#include "duckdb/optimizer/rule/regex_optimizations.hpp"

#include "duckdb/execution/expression_executor.hpp"
#include "duckdb/planner/expression/bound_function_expression.hpp"
#include "duckdb/planner/expression/bound_constant_expression.hpp"
#include "duckdb/function/scalar/string_functions.hpp"
#include "duckdb/function/scalar/regexp.hpp"

#include "re2/re2.h"
#include "re2/regexp.h"

namespace duckdb {

RegexOptimizationRule::RegexOptimizationRule(ExpressionRewriter &rewriter) : Rule(rewriter) {
	auto func = make_uniq<FunctionExpressionMatcher>();
	func->function = make_uniq<SpecificFunctionMatcher>("regexp_matches");
	func->policy = SetMatcher::Policy::PARTIAL_ORDERED;
	func->matchers.push_back(make_uniq<ExpressionMatcher>());
	func->matchers.push_back(make_uniq<ConstantExpressionMatcher>());
	func->matchers.push_back(make_uniq<ConstantExpressionMatcher>());

	root = std::move(func);
}

struct LikeString {
	bool exists = false;
	bool escaped = false;
	string escaped_character = "\\";
	string like_string = "";
};

static LikeString GetLikeStringEscaped(duckdb_re2::Regexp *regexp, bool contains = false) {
	D_ASSERT(regexp->op() == duckdb_re2::kRegexpLiteralString || regexp->op() == duckdb_re2::kRegexpLiteral);
	LikeString ret;
	// case insensitivity may be on now, but it can also turn off.
	if (regexp->op() == duckdb_re2::kRegexpLiteralString) {
		auto nrunes = (idx_t)regexp->nrunes();
		auto runes = regexp->runes();
		for (idx_t i = 0; i < nrunes; i++) {
			// don't bother trying to convert regex with control characters to like. Let
			// RE2 handle that
			if (iscntrl(runes[i])) {
				ret.exists = false;
				return ret;
			}
			char chr = toascii(runes[i]);
			// if a character is equal to the escaped character and the like string is already escaped`
			if (!contains && (chr == '%' || chr == '_' || chr == ret.escaped_character[0])) {
				ret.escaped = true;
				ret.like_string += ret.escaped_character;
			}
			auto run_as_str {chr};
			ret.like_string += run_as_str;
		}
	} else {
		auto rune = regexp->rune();
		if (iscntrl(rune)) {
			ret.exists = false;
			return ret;
		}
		char chr = toascii(rune);
		// if a character is equal to the escaped character return that there is no escaped like string.
		if (!contains && (chr == '%' || chr == '_' || chr == ret.escaped_character[0])) {
			ret.escaped = true;
			ret.like_string += ret.escaped_character;
		}
		auto rune_as_str {chr};
		ret.like_string += rune_as_str;
	}
	D_ASSERT(ret.like_string.size() >= 1);
	ret.exists = true;
	return ret;
}

static LikeString LikeMatchFromRegex(duckdb_re2::RE2 &pattern) {
	LikeString ret = LikeString();
	auto num_subs = pattern.Regexp()->nsub();
	auto subs = pattern.Regexp()->sub();
	auto cur_sub_index = 0;
	while (cur_sub_index < num_subs) {
		switch (subs[cur_sub_index]->op()) {
		case duckdb_re2::kRegexpAnyChar:
			if (cur_sub_index == 0) {
				ret.like_string += "%";
			}
			ret.like_string += "_";
			if (cur_sub_index + 1 == num_subs) {
				ret.like_string += "%";
			}
			break;
		case duckdb_re2::kRegexpStar:
			// .* is a Star operator is a anyChar operator as a child.
			// any other child operator would represent a pattern LIKE cannot match.
			if (subs[cur_sub_index]->nsub() == 1 && subs[cur_sub_index]->sub()[0]->op() == duckdb_re2::kRegexpAnyChar) {
				ret.like_string += "%";
				break;
			}
			return ret;
		case duckdb_re2::kRegexpLiteralString:
		case duckdb_re2::kRegexpLiteral: {
			// if this is the only matching op, we should have directly called
			// GetEscapedLikeString
			D_ASSERT(!(cur_sub_index == 0 && cur_sub_index + 1 == num_subs));
			if (cur_sub_index == 0) {
				ret.like_string += "%";
			}
			// if the kRegexpLiteral or kRegexpLiteralString is the only op to match
			// the string can directly be converted into a contains
			LikeString escaped_like_string = GetLikeStringEscaped(subs[cur_sub_index], false);
			if (!escaped_like_string.exists) {
				return escaped_like_string;
			}
			ret.like_string += escaped_like_string.like_string;
			ret.escaped = escaped_like_string.escaped;
			if (cur_sub_index + 1 == num_subs) {
				ret.like_string += "%";
			}
			break;
		}
		case duckdb_re2::kRegexpEndText:
		case duckdb_re2::kRegexpEmptyMatch:
		case duckdb_re2::kRegexpBeginText: {
			break;
		}
		default:
			// some other regexp op that doesn't have an equivalent to a like string
			// return false;
			return ret;
		}
		cur_sub_index += 1;
	}
	ret.exists = true;
	return ret;
}

unique_ptr<Expression> RegexOptimizationRule::Apply(LogicalOperator &op, vector<reference<Expression>> &bindings,
                                                    bool &changes_made, bool is_root) {
	auto &root = bindings[0].get().Cast<BoundFunctionExpression>();
	auto &constant_expr = bindings[2].get().Cast<BoundConstantExpression>();
	D_ASSERT(root.children.size() == 2 || root.children.size() == 3);
	auto regexp_bind_data = root.bind_info.get()->Cast<RegexpMatchesBindData>();

	auto constant_value = ExpressionExecutor::EvaluateScalar(GetContext(), constant_expr);
	D_ASSERT(constant_value.type() == constant_expr.return_type);
	auto patt_str = StringValue::Get(constant_value);

	duckdb_re2::RE2::Options parsed_options = regexp_bind_data.options;

	if (constant_expr.value.IsNull()) {
		return make_uniq<BoundConstantExpression>(Value(root.return_type));
	}

	// the constant_expr is a scalar expression that we have to fold
	if (!constant_expr.IsFoldable()) {
		return nullptr;
	}

	constant_value = ExpressionExecutor::EvaluateScalar(GetContext(), constant_expr);
	D_ASSERT(constant_value.type() == constant_expr.return_type);
	patt_str = StringValue::Get(constant_value);

	duckdb_re2::RE2 pattern(patt_str, parsed_options);
	if (!pattern.ok()) {
		return nullptr; // this should fail somewhere else
	}

	LikeString like_string;
	if (pattern.Regexp()->parse_flags() & duckdb_re2::Regexp::FoldCase ||
	    !(pattern.Regexp()->parse_flags() & duckdb_re2::Regexp::OneLine)) {
		// parse flags can turn on and off within a regex match, return no optimization
		// TODO: logic to attempt the optimization, then if the parse flags change, then abort
		return nullptr;
	}
	// check for a like string. If we can convert it to a like string, the like string
	// optimizer will further optimize suffix and prefix things.
	if (pattern.Regexp()->op() == duckdb_re2::kRegexpLiteralString ||
	    pattern.Regexp()->op() == duckdb_re2::kRegexpLiteral) {
		// convert to contains.
		LikeString escaped_like_string = GetLikeStringEscaped(pattern.Regexp(), true);
		if (!escaped_like_string.exists) {
			return nullptr;
		}
		auto parameter = make_uniq<BoundConstantExpression>(Value(std::move(escaped_like_string.like_string)));
		auto contains = make_uniq<BoundFunctionExpression>(root.return_type, ContainsFun::GetFunction(),
		                                                   std::move(root.children), nullptr);
		contains->children[1] = std::move(parameter);

		return std::move(contains);
	} else if (pattern.Regexp()->op() == duckdb_re2::kRegexpConcat) {
		like_string = LikeMatchFromRegex(pattern);
	}

	if (!like_string.exists) {
		return nullptr;
	}

	// if regexp had options, remove them the new Like Expression can be matched with.
	if (root.children.size() == 3) {
		root.children.pop_back();
		D_ASSERT(root.children.size() == 2);
	}

	if (like_string.escaped) {
		auto like_escape_expression = make_uniq<BoundFunctionExpression>(
		    root.return_type, LikeEscapeFun::GetLikeEscapeFun(), std::move(root.children), nullptr);
		auto parameter = make_uniq<BoundConstantExpression>(Value(std::move(like_string.like_string)));
		auto escape = make_uniq<BoundConstantExpression>(Value(std::move(like_string.escaped_character)));
		like_escape_expression->children[1] = std::move(parameter);
		like_escape_expression->children.push_back(std::move(escape));
		return std::move(like_escape_expression);
	}
	auto like_expression = make_uniq<BoundFunctionExpression>(root.return_type, LikeFun::GetLikeFunction(),
	                                                          std::move(root.children), nullptr);
	auto parameter = make_uniq<BoundConstantExpression>(Value(std::move(like_string.like_string)));
	like_expression->children[1] = std::move(parameter);
	return std::move(like_expression);
}

} // namespace duckdb

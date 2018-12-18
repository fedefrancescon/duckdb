//===----------------------------------------------------------------------===//
//                         DuckDB
//
// parser/expression/groupref_expression.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "parser/expression.hpp"
#include "parser/sql_node_visitor.hpp"

namespace duckdb {
//! Represents a reference to one of the GROUP BY columns
class GroupRefExpression : public Expression {
public:
	GroupRefExpression(TypeId return_type, size_t group_index);

	unique_ptr<Expression> Accept(SQLNodeVisitor *v) override {
		return v->Visit(*this);
	}
	ExpressionClass GetExpressionClass() override {
		return ExpressionClass::GROUP_REF;
	}

	unique_ptr<Expression> Copy() override;
	
	uint64_t Hash() const override;
	bool Equals(const Expression *other_) const override;

	bool IsScalar() override {
		return false;
	}

	//! The index of the group this expression is referencing
	size_t group_index;
};
} // namespace duckdb

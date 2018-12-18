//===----------------------------------------------------------------------===//
//                         DuckDB
//
// parser/expression/function_expression.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "parser/expression.hpp"
#include "parser/sql_node_visitor.hpp"

namespace duckdb {
class ScalarFunctionCatalogEntry;

//! Represents a function call
class FunctionExpression : public Expression {
public:
	FunctionExpression(string schema_name, string function_name, vector<unique_ptr<Expression>> &children);
	FunctionExpression(string function_name, vector<unique_ptr<Expression>> &children)
	    : FunctionExpression(DEFAULT_SCHEMA, function_name, children) {
	}

	void ResolveType() override;

	unique_ptr<Expression> Accept(SQLNodeVisitor *v) override {
		return v->Visit(*this);
	}
	ExpressionClass GetExpressionClass() override {
		return ExpressionClass::FUNCTION;
	}

	unique_ptr<Expression> Copy() override;

	uint64_t Hash() const override;
	bool Equals(const Expression *other) const override;

	//! Serializes an Expression to a stand-alone binary blob
	void Serialize(Serializer &serializer) override;
	//! Deserializes a blob back into an ConstantExpression
	static unique_ptr<Expression> Deserialize(ExpressionDeserializeInfo *info, Deserializer &source);

	//! Schema of the function
	string schema;
	//! Function name
	string function_name;

	// FIXME: remove this
	ScalarFunctionCatalogEntry *bound_function;
};
} // namespace duckdb

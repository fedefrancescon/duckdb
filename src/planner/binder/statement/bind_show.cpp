#include "duckdb/planner/binder.hpp"
#include "duckdb/parser/statement/show_statement.hpp"
#include "duckdb/planner/operator/logical_show.hpp"

namespace duckdb {

BoundStatement Binder::Bind(ShowStatement &stmt) {
	BoundStatement result;

	auto plan = Bind(*stmt.info->query);
	stmt.info->types = plan.types;
	stmt.info->aliases = plan.names;

	auto show = make_unique<LogicalShow>(move(plan.plan));
	show->types_select = plan.types;
	show->aliases = plan.names;

	result.plan = move(show);

	result.names = {"Field", "Type", "Null", "Key", "Default", "Extra"};
	result.types = {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR,
	                LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR};
	return result;
}

}

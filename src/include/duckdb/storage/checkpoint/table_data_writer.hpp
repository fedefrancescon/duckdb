//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/storage/checkpoint/table_data_writer.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/storage/checkpoint_manager.hpp"
#include "duckdb/common/unordered_map.hpp"

namespace duckdb {
class UncompressedSegment;
class SegmentStatistics;

//! The table data writer is responsible for writing the data of a table to the block manager
class TableDataWriter {
public:
	TableDataWriter(CheckpointManager &manager, TableCatalogEntry &table);
	~TableDataWriter();

	void WriteTableData(Transaction &transaction);

private:
	void AppendData(idx_t col_idx, Vector &data);

	void CreateSegment(idx_t col_idx);
	void FlushSegment(idx_t col_idx);

	void WriteDataPointers();

private:
	CheckpointManager &manager;
	TableCatalogEntry &table;

	vector<unique_ptr<UncompressedSegment>> segments;
	vector<unique_ptr<SegmentStatistics>> stats;

	vector<vector<DataPointer>> data_pointers;
};

} // namespace duckdb

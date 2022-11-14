/*-------------------------------------------------------------------------
 *
 * pg_controldata.c
 *
 * Routines to expose the contents of the control data file via
 * a set of SQL functions.
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  src/backend/utils/misc/pg_controldata.c
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/htup_details.h"
#include "access/transam.h"
#include "access/xlog.h"
#include "access/xlog_internal.h"
#include "catalog/pg_control.h"
#include "catalog/pg_type.h"
#include "common/controldata_utils.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "utils/builtins.h"
#include "utils/pg_lsn.h"
#include "utils/timestamp.h"

Datum
pg_control_system(PG_FUNCTION_ARGS)
{
	Datum		values[4];
	bool		nulls[4];
	TupleDesc	tupdesc;
	HeapTuple	htup;
	ControlFileData *ControlFile;
	bool		crc_ok;

	/*
	 * Construct a tuple descriptor for the result row.  This must match this
	 * function's pg_proc entry!
	 */
	tupdesc = CreateTemplateTupleDesc(4);
	TupleDescInitEntry(tupdesc, (AttrNumber) 1, "pg_control_version",
					   INT4OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 2, "catalog_version_no",
					   INT4OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 3, "system_identifier",
					   INT8OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 4, "pg_control_last_modified",
					   TIMESTAMPTZOID, -1, 0);
	tupdesc = BlessTupleDesc(tupdesc);

	/* read the control file */
	ControlFile = get_controlfile(DataDir, &crc_ok);
	if (!crc_ok)
		ereport(ERROR,
				(errmsg("calculated CRC checksum does not match value stored in file")));

	values[0] = Int32GetDatum(ControlFile->pg_control_version);
	nulls[0] = false;

	values[1] = Int32GetDatum(ControlFile->catalog_version_no);
	nulls[1] = false;

	values[2] = Int64GetDatum(ControlFile->system_identifier);
	nulls[2] = false;

	values[3] = TimestampTzGetDatum(time_t_to_timestamptz(ControlFile->time));
	nulls[3] = false;

	htup = heap_form_tuple(tupdesc, values, nulls);

	PG_RETURN_DATUM(HeapTupleGetDatum(htup));
}

Datum
pg_control_checkpoint(PG_FUNCTION_ARGS)
{
	Datum		values[18];
	bool		nulls[18];
	TupleDesc	tupdesc;
	HeapTuple	htup;
	ControlFileData *ControlFile;
	XLogSegNo	segno;
	char		xlogfilename[MAXFNAMELEN];
	bool		crc_ok;

	/*
	 * Construct a tuple descriptor for the result row.  This must match this
	 * function's pg_proc entry!
	 */
	tupdesc = CreateTemplateTupleDesc(18);
	TupleDescInitEntry(tupdesc, (AttrNumber) 1, "checkpoint_lsn",
					   PG_LSNOID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 2, "redo_lsn",
					   PG_LSNOID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 3, "redo_wal_file",
					   TEXTOID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 4, "timeline_id",
					   INT4OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 5, "prev_timeline_id",
					   INT4OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 6, "full_page_writes",
					   BOOLOID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 7, "next_xid",
					   TEXTOID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 8, "next_oid",
					   OIDOID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 9, "next_multixact_id",
					   XIDOID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 10, "next_multi_offset",
					   XIDOID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 11, "oldest_xid",
					   XIDOID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 12, "oldest_xid_dbid",
					   OIDOID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 13, "oldest_active_xid",
					   XIDOID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 14, "oldest_multi_xid",
					   XIDOID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 15, "oldest_multi_dbid",
					   OIDOID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 16, "oldest_commit_ts_xid",
					   XIDOID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 17, "newest_commit_ts_xid",
					   XIDOID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 18, "checkpoint_time",
					   TIMESTAMPTZOID, -1, 0);
	tupdesc = BlessTupleDesc(tupdesc);

	/* Read the control file. */
	ControlFile = get_controlfile(DataDir, &crc_ok);
	if (!crc_ok)
		ereport(ERROR,
				(errmsg("calculated CRC checksum does not match value stored in file")));

	/*
	 * Calculate name of the WAL file containing the latest checkpoint's REDO
	 * start point.
	 */
	XLByteToSeg(ControlFile->checkPointCopy.redo, segno, wal_segment_size);
	XLogFileName(xlogfilename, ControlFile->checkPointCopy.ThisTimeLineID,
				 segno, wal_segment_size);

	/* Populate the values and null arrays */
	values[0] = LSNGetDatum(ControlFile->checkPoint);
	nulls[0] = false;

	values[1] = LSNGetDatum(ControlFile->checkPointCopy.redo);
	nulls[1] = false;

	values[2] = CStringGetTextDatum(xlogfilename);
	nulls[2] = false;

	values[3] = Int32GetDatum(ControlFile->checkPointCopy.ThisTimeLineID);
	nulls[3] = false;

	values[4] = Int32GetDatum(ControlFile->checkPointCopy.PrevTimeLineID);
	nulls[4] = false;

	values[5] = BoolGetDatum(ControlFile->checkPointCopy.fullPageWrites);
	nulls[5] = false;

	values[6] = CStringGetTextDatum(psprintf("%u:%u",
											 EpochFromFullTransactionId(ControlFile->checkPointCopy.nextXid),
											 XidFromFullTransactionId(ControlFile->checkPointCopy.nextXid)));
	nulls[6] = false;

	values[7] = ObjectIdGetDatum(ControlFile->checkPointCopy.nextOid);
	nulls[7] = false;

	values[8] = TransactionIdGetDatum(ControlFile->checkPointCopy.nextMulti);
	nulls[8] = false;

	values[9] = TransactionIdGetDatum(ControlFile->checkPointCopy.nextMultiOffset);
	nulls[9] = false;

	values[10] = TransactionIdGetDatum(ControlFile->checkPointCopy.oldestXid);
	nulls[10] = false;

	values[11] = ObjectIdGetDatum(ControlFile->checkPointCopy.oldestXidDB);
	nulls[11] = false;

	values[12] = TransactionIdGetDatum(ControlFile->checkPointCopy.oldestActiveXid);
	nulls[12] = false;

	values[13] = TransactionIdGetDatum(ControlFile->checkPointCopy.oldestMulti);
	nulls[13] = false;

	values[14] = ObjectIdGetDatum(ControlFile->checkPointCopy.oldestMultiDB);
	nulls[14] = false;

	values[15] = TransactionIdGetDatum(ControlFile->checkPointCopy.oldestCommitTsXid);
	nulls[15] = false;

	values[16] = TransactionIdGetDatum(ControlFile->checkPointCopy.newestCommitTsXid);
	nulls[16] = false;

	values[17] = TimestampTzGetDatum(time_t_to_timestamptz(ControlFile->checkPointCopy.time));
	nulls[17] = false;

	htup = heap_form_tuple(tupdesc, values, nulls);

	PG_RETURN_DATUM(HeapTupleGetDatum(htup));
}

Datum
pg_control_recovery(PG_FUNCTION_ARGS)
{
	Datum		values[5];
	bool		nulls[5];
	TupleDesc	tupdesc;
	HeapTuple	htup;
	ControlFileData *ControlFile;
	bool		crc_ok;

	/*
	 * Construct a tuple descriptor for the result row.  This must match this
	 * function's pg_proc entry!
	 */
	tupdesc = CreateTemplateTupleDesc(5);
	TupleDescInitEntry(tupdesc, (AttrNumber) 1, "min_recovery_end_lsn",
					   PG_LSNOID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 2, "min_recovery_end_timeline",
					   INT4OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 3, "backup_start_lsn",
					   PG_LSNOID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 4, "backup_end_lsn",
					   PG_LSNOID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 5, "end_of_backup_record_required",
					   BOOLOID, -1, 0);
	tupdesc = BlessTupleDesc(tupdesc);

	/* read the control file */
	ControlFile = get_controlfile(DataDir, &crc_ok);
	if (!crc_ok)
		ereport(ERROR,
				(errmsg("calculated CRC checksum does not match value stored in file")));

	values[0] = LSNGetDatum(ControlFile->minRecoveryPoint);
	nulls[0] = false;

	values[1] = Int32GetDatum(ControlFile->minRecoveryPointTLI);
	nulls[1] = false;

	values[2] = LSNGetDatum(ControlFile->backupStartPoint);
	nulls[2] = false;

	values[3] = LSNGetDatum(ControlFile->backupEndPoint);
	nulls[3] = false;

	values[4] = BoolGetDatum(ControlFile->backupEndRequired);
	nulls[4] = false;

	htup = heap_form_tuple(tupdesc, values, nulls);

	PG_RETURN_DATUM(HeapTupleGetDatum(htup));
}

Datum
pg_control_init(PG_FUNCTION_ARGS)
{
	Datum		values[11];
	bool		nulls[11];
	TupleDesc	tupdesc;
	HeapTuple	htup;
	ControlFileData *ControlFile;
	bool		crc_ok;

	/*
	 * Construct a tuple descriptor for the result row.  This must match this
	 * function's pg_proc entry!
	 */
	tupdesc = CreateTemplateTupleDesc(11);
	TupleDescInitEntry(tupdesc, (AttrNumber) 1, "max_data_alignment",
					   INT4OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 2, "database_block_size",
					   INT4OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 3, "blocks_per_segment",
					   INT4OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 4, "wal_block_size",
					   INT4OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 5, "bytes_per_wal_segment",
					   INT4OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 6, "max_identifier_length",
					   INT4OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 7, "max_index_columns",
					   INT4OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 8, "max_toast_chunk_size",
					   INT4OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 9, "large_object_chunk_size",
					   INT4OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 10, "float8_pass_by_value",
					   BOOLOID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 11, "data_page_checksum_version",
					   INT4OID, -1, 0);
	tupdesc = BlessTupleDesc(tupdesc);

	/* read the control file */
	ControlFile = get_controlfile(DataDir, &crc_ok);
	if (!crc_ok)
		ereport(ERROR,
				(errmsg("calculated CRC checksum does not match value stored in file")));

	values[0] = Int32GetDatum(ControlFile->maxAlign);
	nulls[0] = false;

	values[1] = Int32GetDatum(ControlFile->blcksz);
	nulls[1] = false;

	values[2] = Int32GetDatum(ControlFile->relseg_size);
	nulls[2] = false;

	values[3] = Int32GetDatum(ControlFile->xlog_blcksz);
	nulls[3] = false;

	values[4] = Int32GetDatum(ControlFile->xlog_seg_size);
	nulls[4] = false;

	values[5] = Int32GetDatum(ControlFile->nameDataLen);
	nulls[5] = false;

	values[6] = Int32GetDatum(ControlFile->indexMaxKeys);
	nulls[6] = false;

	values[7] = Int32GetDatum(ControlFile->toast_max_chunk_size);
	nulls[7] = false;

	values[8] = Int32GetDatum(ControlFile->loblksize);
	nulls[8] = false;

	values[9] = BoolGetDatum(ControlFile->float8ByVal);
	nulls[9] = false;

	values[10] = Int32GetDatum(ControlFile->data_checksum_version);
	nulls[10] = false;

	htup = heap_form_tuple(tupdesc, values, nulls);

	PG_RETURN_DATUM(HeapTupleGetDatum(htup));
}

/*
 * pg_operation_log
 *
 * Returns list of operation log data.
 * NOTE: this is a set-returning-function (SRF).
 */
Datum
pg_operation_log(PG_FUNCTION_ARGS)
{
#define PG_OPERATION_LOG_COLS	6
	FuncCallContext *funcctx;
	OperationLogBuffer *log_buffer;

	/*
	 * Initialize tuple descriptor & function call context.
	 */
	if (SRF_IS_FIRSTCALL())
	{
		MemoryContext oldcxt;
		TupleDesc	tupdesc;
		bool		crc_ok;

		/* create a function context for cross-call persistence */
		funcctx = SRF_FIRSTCALL_INIT();

		/* switch to memory context appropriate for multiple function calls */
		oldcxt = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		/* read the control file */
		log_buffer = get_controlfile_log(DataDir, &crc_ok);
		if (!crc_ok)
			ereport(ERROR,
					(errmsg("calculated CRC checksum does not match value stored in file")));

		tupdesc = CreateTemplateTupleDesc(PG_OPERATION_LOG_COLS);

		TupleDescInitEntry(tupdesc, (AttrNumber) 1, "event",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 2, "edition",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 3, "version",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 4, "lsn",
						   PG_LSNOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 5, "last",
						   TIMESTAMPTZOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 6, "count",
						   INT4OID, -1, 0);

		funcctx->tuple_desc = BlessTupleDesc(tupdesc);

		/* The only state we need is the operation log buffer. */
		funcctx->user_fctx = (void *) log_buffer;

		MemoryContextSwitchTo(oldcxt);
	}

	/* stuff done on every call of the function */
	funcctx = SRF_PERCALL_SETUP();
	log_buffer = (OperationLogBuffer *) funcctx->user_fctx;

	if (funcctx->call_cntr < get_operation_log_count(log_buffer))
	{
		Datum		result;
		Datum		values[PG_OPERATION_LOG_COLS];
		bool		nulls[PG_OPERATION_LOG_COLS];
		HeapTuple	tuple;
		OperationLogData *data = get_operation_log_element(log_buffer, (uint16) funcctx->call_cntr);
		int			major_version,
					minor_version,
					patch_version;

		/*
		 * Form tuple with appropriate data.
		 */
		MemSet(nulls, 0, sizeof(nulls));
		MemSet(values, 0, sizeof(values));

		/* event */
		values[0] = CStringGetTextDatum(get_operation_log_type_name(data->ol_type));

		/* edition */
		values[1] = CStringGetTextDatum(get_str_edition(data->ol_edition));

		/* version */
		patch_version = data->ol_version % 100;
		minor_version = (data->ol_version / 100) % 100;
		major_version = data->ol_version / 10000;
		if (major_version < 1000)
			values[2] = CStringGetTextDatum(psprintf("%u.%u.%u.%u", major_version / 100,
													 major_version % 100,
													 minor_version, patch_version));
		else
			values[2] = CStringGetTextDatum(psprintf("%u.%u.%u", major_version / 100,
													 minor_version, patch_version));

		/* lsn */
		values[3] = LSNGetDatum(data->ol_lsn);

		/* last */
		values[4] = TimestampTzGetDatum(time_t_to_timestamptz(data->ol_timestamp));

		/* count */
		values[5] = Int32GetDatum(data->ol_count);

		tuple = heap_form_tuple(funcctx->tuple_desc, values, nulls);
		result = HeapTupleGetDatum(tuple);
		SRF_RETURN_NEXT(funcctx, result);
	}

	/* done when there are no more elements left */
	SRF_RETURN_DONE(funcctx);
}

/*-------------------------------------------------------------------------
 *
 * controldata_utils.c
 *		Common code for control data file output.
 *
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/common/controldata_utils.c
 *
 *-------------------------------------------------------------------------
 */

#ifndef FRONTEND
#include "postgres.h"
#else
#include "postgres_fe.h"
#endif

#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

#include "access/xlog_internal.h"
#include "catalog/pg_control.h"
#include "common/controldata_utils.h"
#include "common/file_perm.h"
#ifdef FRONTEND
#include "common/logging.h"
#endif
#include "port/pg_crc32c.h"

#ifndef FRONTEND
#include "pgstat.h"
#include "storage/fd.h"
#endif

/*
 * Descriptions of supported operations of operation log.
 */
OperationLogTypeDesc OperationLogTypesDescs[] = {
	{OLT_BOOTSTRAP, OLM_INSERT, "bootstrap"},
	{OLT_STARTUP, OLM_MERGE, "startup"},
	{OLT_RESETWAL, OLM_MERGE, "pg_resetwal"},
	{OLT_REWIND, OLM_MERGE, "pg_rewind"},
	{OLT_UPGRADE, OLM_INSERT, "pg_upgrade"},
	{OLT_PROMOTED, OLM_INSERT, "promoted"}
};

/*
 * get_controlfile_with_log()
 *
 * Get controlfile values. The result is a palloc'd copy of the control file
 * data. If log_buffer is not null then result is also a palloc'd buffer with
 * operation log.
 *
 * crc_ok_p can be used by the caller to see whether the CRC of the control
 * file data is correct.
 */
static void
get_controlfile_with_log(const char *DataDir, bool *crc_ok_p,
						 ControlFileData **control_file,
						 OperationLogBuffer * *log_buffer)
{
	ControlFileData *ControlFile = NULL;
	OperationLogBuffer *LogBuffer = NULL;
	int			fd;
	char		ControlFilePath[MAXPGPATH];
	pg_crc32c	crc;
	int			r;

	Assert(crc_ok_p);

	snprintf(ControlFilePath, MAXPGPATH, "%s/global/pg_control", DataDir);

#ifndef FRONTEND
	if ((fd = OpenTransientFile(ControlFilePath, O_RDONLY | PG_BINARY)) == -1)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not open file \"%s\" for reading: %m",
						ControlFilePath)));
#else
	if ((fd = open(ControlFilePath, O_RDONLY | PG_BINARY, 0)) == -1)
		pg_fatal("could not open file \"%s\" for reading: %m",
				 ControlFilePath);
#endif

	if (control_file)
	{
		ControlFile = palloc(sizeof(ControlFileData));

		r = read(fd, ControlFile, sizeof(ControlFileData));
		if (r != sizeof(ControlFileData))
		{
			if (r < 0)
#ifndef FRONTEND
				ereport(ERROR,
						(errcode_for_file_access(),
						 errmsg("could not read file \"%s\": %m", ControlFilePath)));
#else
				pg_fatal("could not read file \"%s\": %m", ControlFilePath);
#endif
			else
#ifndef FRONTEND
				ereport(ERROR,
						(errcode(ERRCODE_DATA_CORRUPTED),
						 errmsg("could not read file \"%s\": read %d of %zu",
								ControlFilePath, r, sizeof(ControlFileData))));
#else
				pg_fatal("could not read file \"%s\": read %d of %zu",
						 ControlFilePath, r, sizeof(ControlFileData));
#endif
		}
		*control_file = ControlFile;
	}

	if (log_buffer)
	{
		if (lseek(fd, PG_OPERATION_LOG_POS, SEEK_SET) != PG_OPERATION_LOG_POS)
#ifndef FRONTEND
			ereport(ERROR,
					(errcode_for_file_access(),
					 errmsg("could not seek to position %d of file \"%s\": %m",
							PG_OPERATION_LOG_POS, ControlFilePath)));
#else
			pg_fatal("could not seek to position %d of file \"%s\": %m",
					 PG_OPERATION_LOG_POS, ControlFilePath);
#endif

		LogBuffer = palloc(PG_OPERATION_LOG_FULL_SIZE);

		r = read(fd, LogBuffer, PG_OPERATION_LOG_FULL_SIZE);
		if (r != PG_OPERATION_LOG_FULL_SIZE)
		{
#ifndef FRONTEND
			ereport(ERROR,
					(errcode(ERRCODE_DATA_CORRUPTED),
					 errmsg("could not read operation log from the file \"%s\": read %d of %d",
							ControlFilePath, r, PG_OPERATION_LOG_FULL_SIZE)));
#else
			pg_fatal("could not read operation log from the file \"%s\": read %d of %d",
					 ControlFilePath, r, PG_OPERATION_LOG_FULL_SIZE);
#endif
		}
		*log_buffer = LogBuffer;
	}

#ifndef FRONTEND
	if (CloseTransientFile(fd) != 0)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not close file \"%s\": %m",
						ControlFilePath)));
#else
	if (close(fd) != 0)
		pg_fatal("could not close file \"%s\": %m", ControlFilePath);
#endif

	if (control_file)
	{
		/* Check the CRC. */
		INIT_CRC32C(crc);
		COMP_CRC32C(crc,
					(char *) ControlFile,
					offsetof(ControlFileData, crc));
		FIN_CRC32C(crc);

		*crc_ok_p = EQ_CRC32C(crc, ControlFile->crc);

		/* Make sure the control file is valid byte order. */
		if (ControlFile->pg_control_version % 65536 == 0 &&
			ControlFile->pg_control_version / 65536 != 0)
#ifndef FRONTEND
			elog(ERROR, _("byte ordering mismatch"));
#else
			pg_log_warning("possible byte ordering mismatch\n"
						   "The byte ordering used to store the pg_control file might not match the one\n"
						   "used by this program.  In that case the results below would be incorrect, and\n"
						   "the PostgreSQL installation would be incompatible with this data directory.");
#endif
	}
	else
		*crc_ok_p = true;

	/*
	 * Do not check CRC of operation log if CRC of control file is damaged or
	 * operation log is not initialized.
	 */
	if (log_buffer && *crc_ok_p && LogBuffer->header.ol_count)
	{
		/* Check the CRC. */
		INIT_CRC32C(crc);
		COMP_CRC32C(crc,
					(char *) LogBuffer + sizeof(pg_crc32c),
					PG_OPERATION_LOG_FULL_SIZE - sizeof(pg_crc32c));
		FIN_CRC32C(crc);

		*crc_ok_p = EQ_CRC32C(crc, LogBuffer->header.ol_crc);
	}
}

/*
 * get_controlfile()
 *
 * Get controlfile values. The result is returned as a palloc'd copy of the
 * control file data.
 *
 * crc_ok_p can be used by the caller to see whether the CRC of the control
 * file data is correct.
 */
ControlFileData *
get_controlfile(const char *DataDir, bool *crc_ok_p)
{
	ControlFileData *ControlFile;

	get_controlfile_with_log(DataDir, crc_ok_p, &ControlFile, NULL);
	return ControlFile;
}

/*
 * get_controlfile_log()
 *
 * Get the operation log ring buffer from controlfile. The result is returned
 * as a palloc'd copy of operation log buffer.
 *
 * crc_ok_p can be used by the caller to see whether the CRC of the operation
 * log is correct.
 */
OperationLogBuffer *
get_controlfile_log(const char *DataDir, bool *crc_ok_p)
{
	OperationLogBuffer *log_buffer;

	get_controlfile_with_log(DataDir, crc_ok_p, NULL, &log_buffer);
	return log_buffer;
}

/*
 * update_controlfile()
 *
 * Update controlfile values with the contents given by caller.  The
 * contents to write are included in "ControlFile". "do_sync" can be
 * optionally used to flush the updated control file.  Note that it is up
 * to the caller to properly lock ControlFileLock when calling this
 * routine in the backend.
 */
void
update_controlfile(const char *DataDir,
				   ControlFileData *ControlFile, bool do_sync)
{
	int			fd;
	char		buffer[PG_CONTROL_FILE_SIZE_WO_LOG];
	char		ControlFilePath[MAXPGPATH];

	/*
	 * Apply the same static assertions as in backend's WriteControlFile().
	 */
	StaticAssertStmt(sizeof(ControlFileData) <= PG_CONTROL_MAX_SAFE_SIZE,
					 "pg_control is too large for atomic disk writes");
	StaticAssertStmt(sizeof(ControlFileData) <= PG_CONTROL_FILE_SIZE,
					 "sizeof(ControlFileData) exceeds PG_CONTROL_FILE_SIZE");

	/* Update timestamp  */
	ControlFile->time = (pg_time_t) time(NULL);

	/* Recalculate CRC of control file */
	INIT_CRC32C(ControlFile->crc);
	COMP_CRC32C(ControlFile->crc,
				(char *) ControlFile,
				offsetof(ControlFileData, crc));
	FIN_CRC32C(ControlFile->crc);

	/*
	 * Write out PG_CONTROL_FILE_SIZE_WO_LOG bytes into pg_control by
	 * zero-padding the excess over sizeof(ControlFileData), to avoid
	 * premature EOF related errors when reading it.
	 */
	memset(buffer, 0, PG_CONTROL_FILE_SIZE_WO_LOG);
	memcpy(buffer, ControlFile, sizeof(ControlFileData));

	snprintf(ControlFilePath, sizeof(ControlFilePath), "%s/%s", DataDir, XLOG_CONTROL_FILE);

#ifndef FRONTEND

	/*
	 * All errors issue a PANIC, so no need to use OpenTransientFile() and to
	 * worry about file descriptor leaks.
	 */
	if ((fd = BasicOpenFile(ControlFilePath, O_RDWR | PG_BINARY)) < 0)
		ereport(PANIC,
				(errcode_for_file_access(),
				 errmsg("could not open file \"%s\": %m",
						ControlFilePath)));
#else
	if ((fd = open(ControlFilePath, O_WRONLY | PG_BINARY,
				   pg_file_create_mode)) == -1)
		pg_fatal("could not open file \"%s\": %m", ControlFilePath);
#endif

	errno = 0;
#ifndef FRONTEND
	pgstat_report_wait_start(WAIT_EVENT_CONTROL_FILE_WRITE_UPDATE);
#endif
	if (write(fd, buffer, PG_CONTROL_FILE_SIZE_WO_LOG) != PG_CONTROL_FILE_SIZE_WO_LOG)
	{
		/* if write didn't set errno, assume problem is no disk space */
		if (errno == 0)
			errno = ENOSPC;

#ifndef FRONTEND
		ereport(PANIC,
				(errcode_for_file_access(),
				 errmsg("could not write file \"%s\": %m",
						ControlFilePath)));
#else
		pg_fatal("could not write file \"%s\": %m", ControlFilePath);
#endif
	}
#ifndef FRONTEND
	pgstat_report_wait_end();
#endif

	if (do_sync)
	{
#ifndef FRONTEND
		pgstat_report_wait_start(WAIT_EVENT_CONTROL_FILE_SYNC_UPDATE);
		if (pg_fsync(fd) != 0)
			ereport(PANIC,
					(errcode_for_file_access(),
					 errmsg("could not fsync file \"%s\": %m",
							ControlFilePath)));
		pgstat_report_wait_end();
#else
		if (fsync(fd) != 0)
			pg_fatal("could not fsync file \"%s\": %m", ControlFilePath);
#endif
	}

	if (close(fd) != 0)
	{
#ifndef FRONTEND
		ereport(PANIC,
				(errcode_for_file_access(),
				 errmsg("could not close file \"%s\": %m",
						ControlFilePath)));
#else
		pg_fatal("could not close file \"%s\": %m", ControlFilePath);
#endif
	}
}

/*
 * update_controlfile_log()
 *
 * Update the operation log ring buffer. "do_sync" can be optionally used to
 * flush the updated control file.  Note that it is up to the caller to
 * properly lock ControlFileLock when calling this routine in the backend.
 */
void
update_controlfile_log(const char *DataDir,
					   OperationLogBuffer * log_buffer, bool do_sync)
{
	int			fd;
	char		ControlFilePath[MAXPGPATH];

	snprintf(ControlFilePath, sizeof(ControlFilePath), "%s/%s", DataDir, XLOG_CONTROL_FILE);

	/* Recalculate CRC of operation log. */
	INIT_CRC32C(log_buffer->header.ol_crc);
	COMP_CRC32C(log_buffer->header.ol_crc,
				(char *) log_buffer + sizeof(pg_crc32c),
				PG_OPERATION_LOG_FULL_SIZE - sizeof(pg_crc32c));
	FIN_CRC32C(log_buffer->header.ol_crc);

#ifndef FRONTEND

	/*
	 * All errors issue a PANIC, so no need to use OpenTransientFile() and to
	 * worry about file descriptor leaks.
	 */
	if ((fd = BasicOpenFile(ControlFilePath, O_RDWR | PG_BINARY)) < 0)
		ereport(PANIC,
				(errcode_for_file_access(),
				 errmsg("could not open file \"%s\": %m",
						ControlFilePath)));
#else
	if ((fd = open(ControlFilePath, O_WRONLY | PG_BINARY,
				   pg_file_create_mode)) == -1)
		pg_fatal("could not open file \"%s\": %m", ControlFilePath);
#endif

	errno = 0;
#ifndef FRONTEND
	pgstat_report_wait_start(WAIT_EVENT_CONTROL_FILE_WRITE_UPDATE);
#endif
	if (lseek(fd, PG_OPERATION_LOG_POS, SEEK_SET) != PG_OPERATION_LOG_POS)
#ifndef FRONTEND
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not seek to position %d of file \"%s\": %m",
						PG_OPERATION_LOG_POS, ControlFilePath)));
#else
		pg_fatal("could not seek to position %d of file \"%s\": %m",
				 PG_OPERATION_LOG_POS, ControlFilePath);
#endif

	if (write(fd, log_buffer, PG_OPERATION_LOG_FULL_SIZE) != PG_OPERATION_LOG_FULL_SIZE)
	{
		/* if write didn't set errno, assume problem is no disk space */
		if (errno == 0)
			errno = ENOSPC;

#ifndef FRONTEND
		ereport(PANIC,
				(errcode_for_file_access(),
				 errmsg("could not write operation log in the file \"%s\": %m",
						ControlFilePath)));
#else
		pg_fatal("could not write operation log in the file \"%s\": %m",
				 ControlFilePath);
#endif
	}
#ifndef FRONTEND
	pgstat_report_wait_end();
#endif

	if (do_sync)
	{
#ifndef FRONTEND
		pgstat_report_wait_start(WAIT_EVENT_CONTROL_FILE_SYNC_UPDATE);
		if (pg_fsync(fd) != 0)
			ereport(PANIC,
					(errcode_for_file_access(),
					 errmsg("could not fsync file \"%s\": %m",
							ControlFilePath)));
		pgstat_report_wait_end();
#else
		if (fsync(fd) != 0)
			pg_fatal("could not fsync file \"%s\": %m", ControlFilePath);
#endif
	}

	if (close(fd) != 0)
	{
#ifndef FRONTEND
		ereport(PANIC,
				(errcode_for_file_access(),
				 errmsg("could not close file \"%s\": %m",
						ControlFilePath)));
#else
		pg_fatal("could not close file \"%s\": %m", ControlFilePath);
#endif
	}
}

/*
 * is_enum_value_correct()
 *
 * Function returns true in case value is correct value of enum.
 *
 * val - test value;
 * minval - first enum value (correct value);
 * maxval - last enum value (incorrect value).
 */
static bool
is_enum_value_correct(int16 val, int16 minval, int16 maxval)
{
	Assert(val >= minval || val < maxval);

	if (val < minval || val >= maxval)
		return false;
	return true;
}

/*
 * get_operation_log_type_desc()
 *
 * Function returns pointer to OperationLogTypeDesc struct for given type of
 * operation ol_type.
 */
static OperationLogTypeDesc *
get_operation_log_type_desc(ol_type_enum ol_type)
{
	return &OperationLogTypesDescs[ol_type - 1];
}

/*
 * fill_operation_log_element()
 *
 * Fill new operation log element. Value of ol_lsn is last checkpoint record
 * pointer.
 */
static void
fill_operation_log_element(ControlFileData *ControlFile,
						   OperationLogTypeDesc * desc,
						   PgNumEdition edition, uint32 version_num,
						   OperationLogData * data)
{
	data->ol_type = desc->ol_type;
	data->ol_edition = edition;
	data->ol_count = 1;
	data->ol_version = version_num;
	data->ol_timestamp = (pg_time_t) time(NULL);
	data->ol_lsn = ControlFile->checkPoint;
}

/*
 * find_operation_log_element_for_merge()
 *
 * Find element into operation log ring buffer by ol_type and version.
 * Returns NULL in case element is not found.
 */
static OperationLogData *
find_operation_log_element_for_merge(ol_type_enum ol_type,
									 OperationLogBuffer * log_buffer,
									 PgNumEdition edition, uint32 version_num)
{
	uint32		first = log_buffer->header.ol_first;
	uint32		count = get_operation_log_count(log_buffer);
	OperationLogData *data;
	uint32		i;

	Assert(first < PG_OPERATION_LOG_COUNT && count <= PG_OPERATION_LOG_COUNT);

	for (i = 0; i < count; i++)
	{
		data = &log_buffer->data[(first + i) % PG_OPERATION_LOG_COUNT];
		if (data->ol_type == ol_type &&
			data->ol_edition == edition &&
			data->ol_version == version_num)
			return data;
	}

	return NULL;
}

/*
 * put_operation_log_element(), put_operation_log_element_version()
 *
 * Put element into operation log ring buffer.
 *
 * DataDir is the path to the top level of the PGDATA directory tree;
 * ol_type is type of operation;
 * edition is edition of current PostgreSQL version;
 * version_num is number of version (for example 13000802 for v13.8.2).
 *
 * Note that it is up to the caller to properly lock ControlFileLock when
 * calling this routine in the backend.
 */
void
put_operation_log_element_version(const char *DataDir, ol_type_enum ol_type,
								  PgNumEdition edition, uint32 version_num)
{
	OperationLogBuffer *log_buffer;
	ControlFileData *ControlFile;
	bool		crc_ok;
	OperationLogTypeDesc *desc;

	if (!is_enum_value_correct(ol_type, OLT_BOOTSTRAP, OLT_NumberOfTypes))
	{
#ifndef FRONTEND
		ereport(ERROR,
				(errcode(ERRCODE_DATA_CORRUPTED),
				 errmsg("invalid type of operation (%u) for operation log", ol_type)));
#else
		pg_fatal("invalid type of operation (%u) for operation log", ol_type);
#endif
	}

	desc = get_operation_log_type_desc(ol_type);

	if (!is_enum_value_correct(desc->ol_mode, OLM_MERGE, OLM_NumberOfModes))
	{
#ifndef FRONTEND
		ereport(ERROR,
				(errcode(ERRCODE_DATA_CORRUPTED),
				 errmsg("invalid mode of operation (%u) for operation log", ol_type)));
#else
		pg_fatal("invalid mode of operation (%u) for operation log", ol_type);
#endif
	}

	get_controlfile_with_log(DataDir, &crc_ok, &ControlFile, &log_buffer);

	if (!crc_ok)
#ifndef FRONTEND
		ereport(ERROR,
				(errcode(ERRCODE_DATA_CORRUPTED),
				 errmsg("pg_control CRC value is incorrect")));
#else
		pg_fatal("pg_control CRC value is incorrect");
#endif

	switch (desc->ol_mode)
	{
		case OLM_MERGE:
			{
				OperationLogData *data;

				data = find_operation_log_element_for_merge(ol_type, log_buffer,
															edition, version_num);
				if (data)
				{
					/*
					 * We just found the element with the same type and the
					 * same version. Update it.
					 */
					if (data->ol_count < PG_UINT16_MAX) /* prevent overflow */
						data->ol_count++;
					data->ol_timestamp = (pg_time_t) time(NULL);
					data->ol_lsn = ControlFile->checkPoint;
					break;
				}
			}
			/* FALLTHROUGH */

		case OLM_INSERT:
			{
				uint16		first = log_buffer->header.ol_first;
				uint16		count = log_buffer->header.ol_count;
				uint16		current;

				Assert(first < PG_OPERATION_LOG_COUNT && count <= PG_OPERATION_LOG_COUNT);

				if (count == PG_OPERATION_LOG_COUNT)
				{
					current = first;
					/* Owerflow, shift the first element */
					log_buffer->header.ol_first = (first + 1) % PG_OPERATION_LOG_COUNT;
				}
				else
				{
					current = first + count;
					/* Increase number of elements: */
					log_buffer->header.ol_count++;
				}

				/* Fill operation log element. */
				fill_operation_log_element(ControlFile, desc, edition, version_num,
										   &log_buffer->data[current]);
				break;
			}

		default:
#ifndef FRONTEND
			ereport(ERROR,
					(errcode(ERRCODE_DATA_CORRUPTED),
					 errmsg("unexpected operation log mode %d",
							desc->ol_mode)));
#else
			pg_fatal("unexpected operation log mode %d", desc->ol_mode);
#endif
	}

	update_controlfile_log(DataDir, log_buffer, true);

	pfree(log_buffer);

	pfree(ControlFile);
}

/*
 * Helper constant for determine current edition.
 * Here can be custom editions.
 */
static const uint8 current_edition = ED_PG_ORIGINAL;

/*
 * Helper constant for determine current version.
 * Multiplier 100 used as reserve of last two digits for patch number.
 */
static const uint32 current_version_num = PG_VERSION_NUM * 100;

void
put_operation_log_element(const char *DataDir, ol_type_enum ol_type)
{
	put_operation_log_element_version(DataDir, ol_type, current_edition, current_version_num);
}

/*
 * get_operation_log_element()
 *
 * Returns operation log buffer element with number num.
 */
OperationLogData *
get_operation_log_element(OperationLogBuffer * log_buffer, uint16 num)
{
	uint32		first = log_buffer->header.ol_first;
#ifdef USE_ASSERT_CHECKING
	uint32		count = get_operation_log_count(log_buffer);

	Assert(num < count);
#endif

	return &log_buffer->data[(first + num) % PG_OPERATION_LOG_COUNT];
}

/*
 * get_operation_log_count()
 *
 * Returns number of elements in given operation log buffer.
 */
uint16
get_operation_log_count(OperationLogBuffer * log_buffer)
{
	return log_buffer->header.ol_count;
}

/*
 * get_operation_log_type_name()
 *
 * Returns name of given type.
 */
const char *
get_operation_log_type_name(ol_type_enum ol_type)
{
	if (is_enum_value_correct(ol_type, OLT_BOOTSTRAP, OLT_NumberOfTypes))
		return OperationLogTypesDescs[ol_type - 1].ol_name;
	else
		return psprintf("unknown name %u", ol_type);
}

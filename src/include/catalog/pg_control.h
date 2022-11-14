/*-------------------------------------------------------------------------
 *
 * pg_control.h
 *	  The system control file "pg_control" is not a heap relation.
 *	  However, we define it here so that the format is documented.
 *
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/catalog/pg_control.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef PG_CONTROL_H
#define PG_CONTROL_H

#include "access/transam.h"
#include "access/xlogdefs.h"
#include "pgtime.h"				/* for pg_time_t */
#include "port/pg_crc32c.h"


/* Version identifier for this pg_control format */
#define PG_CONTROL_VERSION	1300

/* Nonce key length, see below */
#define MOCK_AUTH_NONCE_LEN		32

/*
 * Body of CheckPoint XLOG records.  This is declared here because we keep
 * a copy of the latest one in pg_control for possible disaster recovery.
 * Changing this struct requires a PG_CONTROL_VERSION bump.
 */
typedef struct CheckPoint
{
	XLogRecPtr	redo;			/* next RecPtr available when we began to
								 * create CheckPoint (i.e. REDO start point) */
	TimeLineID	ThisTimeLineID; /* current TLI */
	TimeLineID	PrevTimeLineID; /* previous TLI, if this record begins a new
								 * timeline (equals ThisTimeLineID otherwise) */
	bool		fullPageWrites; /* current full_page_writes */
	FullTransactionId nextXid;	/* next free transaction ID */
	Oid			nextOid;		/* next free OID */
	MultiXactId nextMulti;		/* next free MultiXactId */
	MultiXactOffset nextMultiOffset;	/* next free MultiXact offset */
	TransactionId oldestXid;	/* cluster-wide minimum datfrozenxid */
	Oid			oldestXidDB;	/* database with minimum datfrozenxid */
	MultiXactId oldestMulti;	/* cluster-wide minimum datminmxid */
	Oid			oldestMultiDB;	/* database with minimum datminmxid */
	pg_time_t	time;			/* time stamp of checkpoint */
	TransactionId oldestCommitTsXid;	/* oldest Xid with valid commit
										 * timestamp */
	TransactionId newestCommitTsXid;	/* newest Xid with valid commit
										 * timestamp */

	/*
	 * Oldest XID still running. This is only needed to initialize hot standby
	 * mode from an online checkpoint, so we only bother calculating this for
	 * online checkpoints and only when wal_level is replica. Otherwise it's
	 * set to InvalidTransactionId.
	 */
	TransactionId oldestActiveXid;
} CheckPoint;

/* XLOG info values for XLOG rmgr */
#define XLOG_CHECKPOINT_SHUTDOWN		0x00
#define XLOG_CHECKPOINT_ONLINE			0x10
#define XLOG_NOOP						0x20
#define XLOG_NEXTOID					0x30
#define XLOG_SWITCH						0x40
#define XLOG_BACKUP_END					0x50
#define XLOG_PARAMETER_CHANGE			0x60
#define XLOG_RESTORE_POINT				0x70
#define XLOG_FPW_CHANGE					0x80
#define XLOG_END_OF_RECOVERY			0x90
#define XLOG_FPI_FOR_HINT				0xA0
#define XLOG_FPI						0xB0
/* 0xC0 is used in Postgres 9.5-11 */
#define XLOG_OVERWRITE_CONTRECORD		0xD0


/*
 * System status indicator.  Note this is stored in pg_control; if you change
 * it, you must bump PG_CONTROL_VERSION
 */
typedef enum DBState
{
	DB_STARTUP = 0,
	DB_SHUTDOWNED,
	DB_SHUTDOWNED_IN_RECOVERY,
	DB_SHUTDOWNING,
	DB_IN_CRASH_RECOVERY,
	DB_IN_ARCHIVE_RECOVERY,
	DB_IN_PRODUCTION
} DBState;

/*
 * Contents of pg_control.
 */

typedef struct ControlFileData
{
	/*
	 * Unique system identifier --- to ensure we match up xlog files with the
	 * installation that produced them.
	 */
	uint64		system_identifier;

	/*
	 * Version identifier information.  Keep these fields at the same offset,
	 * especially pg_control_version; they won't be real useful if they move
	 * around.  (For historical reasons they must be 8 bytes into the file
	 * rather than immediately at the front.)
	 *
	 * pg_control_version identifies the format of pg_control itself.
	 * catalog_version_no identifies the format of the system catalogs.
	 *
	 * There are additional version identifiers in individual files; for
	 * example, WAL logs contain per-page magic numbers that can serve as
	 * version cues for the WAL log.
	 */
	uint32		pg_control_version; /* PG_CONTROL_VERSION */
	uint32		catalog_version_no; /* see catversion.h */

	/*
	 * System status data
	 */
	DBState		state;			/* see enum above */
	pg_time_t	time;			/* time stamp of last pg_control update */
	XLogRecPtr	checkPoint;		/* last check point record ptr */

	CheckPoint	checkPointCopy; /* copy of last check point record */

	XLogRecPtr	unloggedLSN;	/* current fake LSN value, for unlogged rels */

	/*
	 * These two values determine the minimum point we must recover up to
	 * before starting up:
	 *
	 * minRecoveryPoint is updated to the latest replayed LSN whenever we
	 * flush a data change during archive recovery. That guards against
	 * starting archive recovery, aborting it, and restarting with an earlier
	 * stop location. If we've already flushed data changes from WAL record X
	 * to disk, we mustn't start up until we reach X again. Zero when not
	 * doing archive recovery.
	 *
	 * backupStartPoint is the redo pointer of the backup start checkpoint, if
	 * we are recovering from an online backup and haven't reached the end of
	 * backup yet. It is reset to zero when the end of backup is reached, and
	 * we mustn't start up before that. A boolean would suffice otherwise, but
	 * we use the redo pointer as a cross-check when we see an end-of-backup
	 * record, to make sure the end-of-backup record corresponds the base
	 * backup we're recovering from.
	 *
	 * backupEndPoint is the backup end location, if we are recovering from an
	 * online backup which was taken from the standby and haven't reached the
	 * end of backup yet. It is initialized to the minimum recovery point in
	 * pg_control which was backed up last. It is reset to zero when the end
	 * of backup is reached, and we mustn't start up before that.
	 *
	 * If backupEndRequired is true, we know for sure that we're restoring
	 * from a backup, and must see a backup-end record before we can safely
	 * start up.
	 */
	XLogRecPtr	minRecoveryPoint;
	TimeLineID	minRecoveryPointTLI;
	XLogRecPtr	backupStartPoint;
	XLogRecPtr	backupEndPoint;
	bool		backupEndRequired;

	/*
	 * Parameter settings that determine if the WAL can be used for archival
	 * or hot standby.
	 */
	int			wal_level;
	bool		wal_log_hints;
	int			MaxConnections;
	int			max_worker_processes;
	int			max_wal_senders;
	int			max_prepared_xacts;
	int			max_locks_per_xact;
	bool		track_commit_timestamp;

	/*
	 * This data is used to check for hardware-architecture compatibility of
	 * the database and the backend executable.  We need not check endianness
	 * explicitly, since the pg_control version will surely look wrong to a
	 * machine of different endianness, but we do need to worry about MAXALIGN
	 * and floating-point format.  (Note: storage layout nominally also
	 * depends on SHORTALIGN and INTALIGN, but in practice these are the same
	 * on all architectures of interest.)
	 *
	 * Testing just one double value is not a very bulletproof test for
	 * floating-point compatibility, but it will catch most cases.
	 */
	uint32		maxAlign;		/* alignment requirement for tuples */
	double		floatFormat;	/* constant 1234567.0 */
#define FLOATFORMAT_VALUE	1234567.0

	/*
	 * This data is used to make sure that configuration of this database is
	 * compatible with the backend executable.
	 */
	uint32		blcksz;			/* data block size for this DB */
	uint32		relseg_size;	/* blocks per segment of large relation */

	uint32		xlog_blcksz;	/* block size within WAL files */
	uint32		xlog_seg_size;	/* size of each WAL segment */

	uint32		nameDataLen;	/* catalog name field width */
	uint32		indexMaxKeys;	/* max number of columns in an index */

	uint32		toast_max_chunk_size;	/* chunk size in TOAST tables */
	uint32		loblksize;		/* chunk size in pg_largeobject */

	bool		float8ByVal;	/* float8, int8, etc pass-by-value? */

	/* Are data pages protected by checksums? Zero if no checksum version */
	uint32		data_checksum_version;

	/*
	 * Random nonce, used in authentication requests that need to proceed
	 * based on values that are cluster-unique, like a SASL exchange that
	 * failed at an early stage.
	 */
	char		mock_authentication_nonce[MOCK_AUTH_NONCE_LEN];

	/* CRC of all above ... MUST BE LAST! */
	pg_crc32c	crc;
} ControlFileData;

/*
 * Maximum safe value of sizeof(ControlFileData).  For reliability's sake,
 * it's critical that pg_control updates be atomic writes.  That generally
 * means the active data can't be more than one disk sector, which is 512
 * bytes on common hardware.  Be very careful about raising this limit.
 */
#define PG_CONTROL_MAX_SAFE_SIZE	512

/*
 * Physical size of the pg_control file.  Note that this is considerably
 * bigger than the actually used size (ie, sizeof(ControlFileData)).
 * The idea is to keep the physical size constant independent of format
 * changes, so that ReadControlFile will deliver a suitable wrong-version
 * message instead of a read error if it's looking at an incompatible file.
 */
#define PG_CONTROL_FILE_SIZE		8192

/*
 * Type of operation for operation log.
 */
typedef enum
{
	OLT_BOOTSTRAP = 1,			/* bootstrap */
	OLT_STARTUP,				/* server startup */
	OLT_RESETWAL,				/* pg_resetwal */
	OLT_REWIND,					/* pg_rewind */
	OLT_UPGRADE,				/* pg_upgrade */
	OLT_PROMOTED,				/* promoted */
	OLT_NumberOfTypes			/* should be last */
}			ol_type_enum;

/*
 * Mode of operation processing.
 */
typedef enum
{
	OLM_MERGE = 1,				/* insert element only if not exists element
								 * with the same ol_type and ol_version;
								 * otherwise update existing element */
	OLM_INSERT,					/* insert element into ring buffer 'as is' */
	OLM_NumberOfModes			/* should be last */
}			ol_mode_enum;

/*
 * Helper struct for describing supported operations.
 */
typedef struct OperationLogTypeDesc
{
	ol_type_enum ol_type;		/* element type */
	ol_mode_enum ol_mode;		/* element mode */
	const char *ol_name;		/* display name of element */
}			OperationLogTypeDesc;

/*
 * Element of operation log ring buffer (24 bytes).
 */
typedef struct OperationLogData
{
	uint8		ol_type;		/* operation type */
	uint8		ol_edition;		/* postgres edition */
	uint16		ol_count;		/* number of operations */
	uint32		ol_version;		/* postgres version */
	pg_time_t	ol_timestamp;	/* = int64, operation date/time */
	XLogRecPtr	ol_lsn;			/* = uint64, last check point record ptr */
}			OperationLogData;

/*
 * Header of operation log ring buffer (16 bytes).
 */
typedef struct OperationLogHeader
{
	pg_crc32c	ol_crc;			/* CRC of operation log ... MUST BE FIRST! */
	uint16		ol_first;		/* position of first ring buffer element */
	uint16		ol_count;		/* number of elements in ring buffer */
	uint8		ol_pad[8];		/* just for alignment */
}			OperationLogHeader;

/*
 * Whole size of the operation log ring buffer (with header).
 */
#define PG_OPERATION_LOG_FULL_SIZE	4096

/*
 * Size of elements of the operation log ring buffer.
 * Value must be a multiple of sizeof(OperationLogData).
 */
#define PG_OPERATION_LOG_SIZE			(PG_OPERATION_LOG_FULL_SIZE - sizeof(OperationLogHeader))

/*
 * Size of pg_control file without operation log ring buffer.
 */
#define PG_CONTROL_FILE_SIZE_WO_LOG		(PG_CONTROL_FILE_SIZE - PG_OPERATION_LOG_FULL_SIZE)

/*
 * Position of the operation log ring buffer in the control file.
 */
#define PG_OPERATION_LOG_POS			PG_CONTROL_FILE_SIZE_WO_LOG

/*
 * Number of elements in the operation log.
 */
#define PG_OPERATION_LOG_COUNT			(PG_OPERATION_LOG_SIZE / sizeof(OperationLogData))

/*
 * Operation log ring buffer.
 */
typedef struct OperationLogBuffer
{
	OperationLogHeader header;
	OperationLogData data[PG_OPERATION_LOG_COUNT];

}			OperationLogBuffer;

/* Enum for postgres edition. */
typedef enum
{
	ED_PG_ORIGINAL = 0
	/* Here can be custom editions */
}			PgNumEdition;

#define ED_PG_ORIGINAL_STR		"vanilla"
#define ED_UNKNOWN_STR			"unknown"

/*
 * get_str_edition()
 *
 * Returns edition string by edition number.
 */
static inline const char *
get_str_edition(PgNumEdition edition)
{
	switch (edition)
	{
		case ED_PG_ORIGINAL:
			return ED_PG_ORIGINAL_STR;

		/* Here can be custom editions */
	}
	return ED_UNKNOWN_STR;
}

#endif							/* PG_CONTROL_H */

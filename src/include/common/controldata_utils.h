/*
 * controldata_utils.h
 *		Common code for pg_controldata output
 *
 *	Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 *	Portions Copyright (c) 1994, Regents of the University of California
 *
 *	src/include/common/controldata_utils.h
 */
#ifndef COMMON_CONTROLDATA_UTILS_H
#define COMMON_CONTROLDATA_UTILS_H

#include "catalog/pg_control.h"

extern ControlFileData *get_controlfile(const char *DataDir, bool *crc_ok_p);
extern void update_controlfile(const char *DataDir,
							   ControlFileData *ControlFile, bool do_sync);

extern OperationLogBuffer * get_controlfile_log(const char *DataDir, bool *crc_ok_p);
extern void update_controlfile_log(const char *DataDir,
								   OperationLogBuffer * log_buffer, bool do_sync);

extern void put_operation_log_element(const char *DataDir, ol_type_enum ol_type);
extern void put_operation_log_element_version(const char *DataDir, ol_type_enum ol_type,
											  PgNumEdition edition, uint32 version_num);
extern uint16 get_operation_log_count(OperationLogBuffer * log_buffer);
extern OperationLogData * get_operation_log_element(OperationLogBuffer * log_buffer,
													uint16 num);
extern const char *get_operation_log_type_name(ol_type_enum ol_type);

#endif							/* COMMON_CONTROLDATA_UTILS_H */

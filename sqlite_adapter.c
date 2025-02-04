#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "sqlite3.h"
#include "cfstrace.h"
#include "sqlite_adapter.h"

#define create_open "CREATE TABLE IF NOT EXISTS open_table ( hostname TEXT NOT NULL, timestamp INTEGER NOT NULL, pid INTEGER NOT NULL, tid INTEGER NOT NULL, duration INTEGER NOT NULL, name TEXT, flags INTEGER, mode INTEGER DEFAULT (0), ret INTEGER, errno INTEGER DEFAULT (0))"
#define insert_open "INSERT INTO open_table VALUES (@host, @time, @pid, @tid, @duration, @name, @flags, @mode, @ret, @errno)"

#define create_close "CREATE TABLE IF NOT EXISTS close_table ( hostname TEXT NOT NULL, timestamp INTEGER NOT NULL, pid INTEGER NOT NULL, tid INTEGER NOT NULL, duration INTEGER NOT NULL, fd INTEGER NOT NULL, ret INTEGER, errno INTEGER DEFAULT (0))"
#define insert_close "INSERT INTO close_table VALUES (@host, @time, @pid, @tid, @duration, @fd, @ret, @errno)"

#define create_read "CREATE TABLE IF NOT EXISTS read_table (hostname TEXT, timestamp INTEGER, pid INTEGER, tid INTEGER, duration INTEGER, fd INTEGER, count INTEGER, ret INTEGER, errno INTEGER)"
#define insert_read "INSERT INTO read_table VALUES (@host, @time, @pid, @tid, @duration, @fd, @count, @ret, @errno)"

#define create_write "CREATE TABLE IF NOT EXISTS write_table (hostname TEXT, timestamp INTEGER, pid INTEGER, tid INTEGER, duration INTEGER, fd INTEGER, count INTEGER, ret INTEGER, errno INTEGER)"
#define insert_write "INSERT INTO write_table VALUES (@host, @time, @pid, @tid, @duration, @fd, @count, @ret, @errno)"

#define create_proc_start "CREATE TABLE IF NOT EXISTS proc_start_table ( hostname TEXT NOT NULL, timestamp INTEGER NOT NULL, pid INTEGER NOT NULL, name TEXT)"
#define insert_proc_start "INSERT INTO proc_start_table VALUES (@host, @time, @pid, @name)"

#define create_proc_end "CREATE TABLE IF NOT EXISTS proc_end_table ( hostname TEXT NOT NULL, timestamp INTEGER NOT NULL, pid INTEGER NOT NULL)"
#define insert_proc_end "INSERT INTO proc_end_table VALUES (@host, @time, @pid)"

void* transaction_thread(void *sqldb)
{
	sqlite_adapter_t* db = (sqlite_adapter_t*)sqldb;
	sqlite3 *ldb = (sqlite3 *) (*db).db;

	while(1) {
		pthread_mutex_lock(&(*db).sql_mutex);
		sqlite3_exec(ldb, "BEGIN TRANSACTION", NULL, NULL, NULL);
		pthread_mutex_unlock(&(*db).sql_mutex);

		sleep(5);
		pthread_mutex_lock(&(*db).sql_mutex);
		sqlite3_exec(ldb, "END TRANSACTION", NULL, NULL, NULL);
		pthread_mutex_unlock(&(*db).sql_mutex);
	};
	pthread_exit(NULL);
}


sqlite_adapter_t* sqlite_open_database(const char *db_file_name)
{
	sqlite_adapter_t *db = malloc(sizeof(sqlite_adapter_t));

	sqlite3_open(db_file_name, &((*db).db));

	//temp test
	sqlite3_exec((*db).db, "DROP TABLE open_table", NULL, NULL, NULL);
	sqlite3_exec((*db).db, "DROP TABLE close_table", NULL, NULL, NULL);
	sqlite3_exec((*db).db, "DROP TABLE read_table", NULL, NULL, NULL);
	sqlite3_exec((*db).db, "DROP TABLE write_table", NULL, NULL, NULL);
	sqlite3_exec((*db).db, "DROP TABLE proc_start_table", NULL, NULL, NULL);
	sqlite3_exec((*db).db, "DROP TABLE proc_end_table", NULL, NULL, NULL);


	sqlite3_exec((*db).db, create_open, NULL, NULL, NULL);
	sqlite3_exec((*db).db, create_close, NULL, NULL, NULL);
	sqlite3_exec((*db).db, create_read, NULL, NULL, NULL);
	sqlite3_exec((*db).db, create_write, NULL, NULL, NULL);
	sqlite3_exec((*db).db, create_proc_start, NULL, NULL, NULL);
	sqlite3_exec((*db).db, create_proc_end, NULL, NULL, NULL);


	sqlite3_exec((*db).db, "PRAGMA synchronous = OFF", NULL, NULL, NULL);
	sqlite3_exec((*db).db, "PRAGMA journal_mode = MEMORY", NULL, NULL, NULL);
	sqlite3_exec((*db).db, "PRAGMA page_size = 4096", NULL, NULL, NULL);
	sqlite3_exec((*db).db, "PRAGMA cache_size = 4096", NULL, NULL, NULL);
	sqlite3_exec((*db).db, "PRAGMA encoding = \"UTF-8\"", NULL, NULL, NULL);

	strncpy((*db).db_name, db_file_name, 1023); (*db).db_name[1023]=0;

	//prepare statements
	sqlite3_prepare_v2((*db).db, insert_open, strlen(insert_open)+1, &((*db).open_insert_stmt), NULL); sqlite3_clear_bindings((*db).open_insert_stmt); sqlite3_reset((*db).open_insert_stmt);
	sqlite3_prepare_v2((*db).db, insert_close, strlen(insert_close)+1, &((*db).close_insert_stmt), NULL); sqlite3_clear_bindings((*db).close_insert_stmt); sqlite3_reset((*db).close_insert_stmt);
	sqlite3_prepare_v2((*db).db, insert_read, strlen(insert_read)+1, &((*db).read_insert_stmt), NULL); sqlite3_clear_bindings((*db).read_insert_stmt); sqlite3_reset((*db).read_insert_stmt);
	sqlite3_prepare_v2((*db).db, insert_write, strlen(insert_write)+1, &((*db).write_insert_stmt), NULL); sqlite3_clear_bindings((*db).write_insert_stmt); sqlite3_reset((*db).write_insert_stmt);
	sqlite3_prepare_v2((*db).db, insert_proc_start, strlen(insert_proc_start)+1, &((*db).proc_start_insert_stmt), NULL); sqlite3_clear_bindings((*db).proc_start_insert_stmt); sqlite3_reset((*db).proc_start_insert_stmt);
	sqlite3_prepare_v2((*db).db, insert_proc_end, strlen(insert_proc_end)+1, &((*db).proc_end_insert_stmt), NULL); sqlite3_clear_bindings((*db).proc_end_insert_stmt); sqlite3_reset((*db).proc_end_insert_stmt);

	pthread_mutex_init(&(*db).sql_mutex, NULL);
	pthread_create(&(*db).tthread, NULL, transaction_thread, (void*) (*db).db);

	return db;
}


void sqlite_close_database(sqlite_adapter_t *adapter)
{
	if(!adapter)
		return;
	// clean up!
	sqlite3_close((*adapter).db);
	free (adapter);
}

void sqlite_insert_data(sqlite_adapter_t *adapter, const char *hostname, void *data)
{
	sqlite3 *db = (*adapter).db;
	sqlite3_stmt *stmt = NULL;

	enum op_type *type = (enum op_type *)data;
	pthread_mutex_lock(&(*adapter).sql_mutex);
	switch(*type) {
		case READ:
		{
			opfd_t *operation = data;
			/*fprintf(stderr,"[%d:%d] read: %d, %d : %d | duration: %d\n",
				(*operation).pid, (*operation).tid,
				(*operation).data.read_data.fd,
				(*operation).data.read_data.count,
				(*operation).data.read_data.ret,
				(*operation).duration);*/
			stmt = (*adapter).read_insert_stmt;
			//(@host, @time, @pid, @tid, @duration, @fd, @count, @ret, @errno)
			sqlite3_bind_text (stmt, 1, hostname, -1, SQLITE_TRANSIENT);
			sqlite3_bind_int64(stmt, 2, (*operation).header.timestamp);
			sqlite3_bind_int  (stmt, 3, (*operation).header.pid);
			sqlite3_bind_int  (stmt, 4, (*operation).header.tid);
			sqlite3_bind_int64(stmt, 5, (*operation).header.duration);
			sqlite3_bind_int  (stmt, 6, (*operation).data.read_data.fd);
			sqlite3_bind_int  (stmt, 7, (*operation).data.read_data.count);
			sqlite3_bind_int  (stmt, 8, (*operation).data.read_data.ret);
			sqlite3_bind_int  (stmt, 9, (*operation).header.err);

			sqlite3_step(stmt);
			sqlite3_clear_bindings(stmt);
			sqlite3_reset(stmt);
		}
			break;
		case WRITE:
		{
			opfd_t *operation = data;
			/*fprintf(stderr,"[%d:%d] write: %d, %d : %d | duration: %d\n",
				(*operation).pid, (*operation).tid,
				(*operation).data.write_data.fd,
				(*operation).data.write_data.count,
				(*operation).data.write_data.ret,
				(*operation).duration);*/

			stmt = (*adapter).write_insert_stmt;
			//(@host, @time, @pid, @tid, @duration, @fd, @count, @ret, @errno)
			sqlite3_bind_text (stmt, 1, hostname, -1, SQLITE_TRANSIENT);
			sqlite3_bind_int64(stmt, 2, (*operation).header.timestamp);
			sqlite3_bind_int  (stmt, 3, (*operation).header.pid);
			sqlite3_bind_int  (stmt, 4, (*operation).header.tid);
			sqlite3_bind_int64(stmt, 5, (*operation).header.duration);
			sqlite3_bind_int  (stmt, 6, (*operation).data.write_data.fd);
			sqlite3_bind_int  (stmt, 7, (*operation).data.write_data.count);
			sqlite3_bind_int  (stmt, 8, (*operation).data.write_data.ret);
			sqlite3_bind_int  (stmt, 9, (*operation).header.err);

			sqlite3_step(stmt);
			sqlite3_clear_bindings(stmt);
			sqlite3_reset(stmt);
		}
			break;
		case OPEN:
		{
			opname_t *operation = data;
			/*fprintf(stderr,"[%d:%d] open: %s, %d : %d | duration: %d\n",
				(*operation).pid, (*operation).tid,
				(*operation).name,
				(*operation).data.open_data.flags,
				(*operation).data.open_data.ret,
				(*operation).duration);*/

			stmt = (*adapter).open_insert_stmt;
			//@host, @time, @pid, @tid, @duration, @name, @flags, @mode, @ret, @errno
			sqlite3_bind_text (stmt, 1, hostname, -1, SQLITE_TRANSIENT);
			sqlite3_bind_int64(stmt, 2, (*operation).header.timestamp);
			sqlite3_bind_int  (stmt, 3, (*operation).header.pid);
			sqlite3_bind_int  (stmt, 4, (*operation).header.tid);
			sqlite3_bind_int64(stmt, 5, (*operation).header.duration);
			sqlite3_bind_text (stmt, 6, (*operation).name, -1, SQLITE_TRANSIENT);
			sqlite3_bind_int  (stmt, 7, (*operation).data.open_data.flags);
			sqlite3_bind_int  (stmt, 8, (*operation).data.open_data.mode);
			sqlite3_bind_int  (stmt, 9, (*operation).data.open_data.ret);
			sqlite3_bind_int  (stmt, 10, (*operation).header.err);

			sqlite3_step(stmt);
			sqlite3_clear_bindings(stmt);
			sqlite3_reset(stmt);
		}
			break;
		case CLOSE:
		{
			opfd_t *operation = data;

			stmt = (*adapter).close_insert_stmt;
			//(@host, @time, @pid, @tid, @duration, @fd, @ret, @errno)
			sqlite3_bind_text (stmt, 1, hostname, -1, SQLITE_TRANSIENT);
			sqlite3_bind_int64(stmt, 2, (*operation).header.timestamp);
			sqlite3_bind_int  (stmt, 3, (*operation).header.pid);
			sqlite3_bind_int  (stmt, 4, (*operation).header.tid);
			sqlite3_bind_int64(stmt, 5, (*operation).header.duration);
			sqlite3_bind_int  (stmt, 6, (*operation).data.close_data.fd);
			sqlite3_bind_int  (stmt, 7, (*operation).data.close_data.ret);
			sqlite3_bind_int  (stmt, 8, (*operation).header.err);

			sqlite3_step(stmt);
			sqlite3_clear_bindings(stmt);
			sqlite3_reset(stmt);
		}
			break;
		case PROC_START:
		{
			opname_t *operation = data;

			stmt = (*adapter).proc_start_insert_stmt;
			//(@host, @time, @pid, @name)
			sqlite3_bind_text (stmt, 1, hostname, -1, SQLITE_TRANSIENT);
			sqlite3_bind_int64(stmt, 2, (*operation).header.timestamp);
			sqlite3_bind_int  (stmt, 3, (*operation).header.pid);
			sqlite3_bind_text (stmt, 4, (*operation).name, -1, SQLITE_TRANSIENT);

			sqlite3_step(stmt);
			sqlite3_clear_bindings(stmt);
			sqlite3_reset(stmt);
		}
			break;
		case PROC_CLOSE:
		{
			opfd_t *operation = data;

			stmt = (*adapter).proc_end_insert_stmt;
			//(@host, @time, @pid, @name)
			sqlite3_bind_text (stmt, 1, hostname, -1, SQLITE_TRANSIENT);
			sqlite3_bind_int64(stmt, 2, (*operation).header.timestamp);
			sqlite3_bind_int  (stmt, 3, (*operation).header.pid);

			sqlite3_step(stmt);
			sqlite3_clear_bindings(stmt);
			sqlite3_reset(stmt);
		}
			break;
		default:
			printf("This shouldn't not happen! Msg type %d\n", (int)*type);
			break;
	};
	pthread_mutex_unlock(&(*adapter).sql_mutex);
}

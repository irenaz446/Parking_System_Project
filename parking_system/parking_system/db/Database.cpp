/**
 * @file Database.cpp
 * @brief Implementation of the SQLite database wrapper.
 *
 * Schema
 * ───────
 *   prices (id, city UNIQUE, price_per_min, updated_at)
 *   customer_data (id, customer_id, city, start_lat, start_lon,
 *                  end_lat, end_lon, start_time, end_time,
 *                  total_fee, recorded_at)
 *
 * All writes use prepared statements bound at call time, which avoids
 * SQL injection and is faster than building format strings.
 */

#include "Database.hpp"
#include "../common/Logger.hpp"

#include <ctime>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <stdexcept>

/* ── Constructor ─────────────────────────────────────────────────────────── */

Database::Database(const std::string &path)
{
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        std::string err = db_ ? sqlite3_errmsg(db_) : "unknown";
        throw std::runtime_error("sqlite3_open('" + path + "'): " + err);
    }
    // Enable WAL mode for better concurrent read/write performance
    exec("PRAGMA journal_mode=WAL;");
    exec("PRAGMA synchronous=NORMAL;");
    createSchema();
    Logger::info("Database opened: " + path);
}

/* ── Destructor ──────────────────────────────────────────────────────────── */

Database::~Database()
{
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

/* ── Schema creation ─────────────────────────────────────────────────────── */

void Database::createSchema()
{
    exec(R"(
        CREATE TABLE IF NOT EXISTS prices (
            id            INTEGER PRIMARY KEY AUTOINCREMENT,
            city          TEXT    NOT NULL UNIQUE,
            price_per_min REAL    NOT NULL,
            updated_at    TEXT    NOT NULL
        );
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS customer_data (
            id            INTEGER PRIMARY KEY AUTOINCREMENT,
            customer_id   TEXT    NOT NULL,
            city          TEXT    NOT NULL,
            start_lat     REAL,
            start_lon     REAL,
            end_lat       REAL,
            end_lon       REAL,
            start_time    TEXT,
            end_time      TEXT,
            total_fee     REAL,
            recorded_at   TEXT    NOT NULL
        );
    )");

    Logger::info("Database schema verified");
}

/* ── Price operations ────────────────────────────────────────────────────── */

void Database::upsertPrice(const std::string &city, double pricePerMin)
{
    const char *sql =
        "INSERT INTO prices (city, price_per_min, updated_at) "
        "VALUES (?, ?, ?) "
        "ON CONFLICT(city) DO UPDATE SET "
        "  price_per_min = excluded.price_per_min, "
        "  updated_at    = excluded.updated_at;";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        Logger::error("upsertPrice prepare: " + std::string(sqlite3_errmsg(db_)));
        return;
    }

    std::string now = nowStr();
    sqlite3_bind_text  (stmt, 1, city.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, pricePerMin);
    sqlite3_bind_text  (stmt, 3, now.c_str(),  -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE)
        Logger::error("upsertPrice step: " + std::string(sqlite3_errmsg(db_)));

    sqlite3_finalize(stmt);
}

void Database::deletePrice(const std::string &city)
{
    const char *sql = "DELETE FROM prices WHERE city = ?;";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        Logger::error("deletePrice prepare: " + std::string(sqlite3_errmsg(db_)));
        return;
    }

    sqlite3_bind_text(stmt, 1, city.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE)
        Logger::error("deletePrice step: " + std::string(sqlite3_errmsg(db_)));

    sqlite3_finalize(stmt);
    Logger::info("Deleted price for city: " + city);
}

/* ── Record insertion ────────────────────────────────────────────────────── */

void Database::insertRecord(const parking_record_t &rec)
{
    const char *sql =
        "INSERT INTO customer_data "
        "(customer_id, city, start_lat, start_lon, "
        " end_lat, end_lon, start_time, end_time, total_fee, recorded_at) "
        "VALUES (?,?,?,?,?,?,?,?,?,?);";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        Logger::error("insertRecord prepare: " + std::string(sqlite3_errmsg(db_)));
        return;
    }

    // Format timestamps
    auto fmtTime = [](time_t t) -> std::string {
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S",
                      std::localtime(&t));
        return buf;
    };

    std::string startTs = fmtTime(rec.start_time);
    std::string endTs   = fmtTime(rec.end_time);
    std::string nowTs   = nowStr();

    sqlite3_bind_text  (stmt,  1, rec.customer_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (stmt,  2, rec.city,         -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt,  3, rec.start_lat);
    sqlite3_bind_double(stmt,  4, rec.start_lon);
    sqlite3_bind_double(stmt,  5, rec.end_lat);
    sqlite3_bind_double(stmt,  6, rec.end_lon);
    sqlite3_bind_text  (stmt,  7, startTs.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (stmt,  8, endTs.c_str(),    -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt,  9, rec.total_fee);
    sqlite3_bind_text  (stmt, 10, nowTs.c_str(),    -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE)
        Logger::error("insertRecord step: " + std::string(sqlite3_errmsg(db_)));
    else
        Logger::info("Persisted record: customer=" + std::string(rec.customer_id)
                     + " fee=" + std::to_string(rec.total_fee));

    sqlite3_finalize(stmt);
}

/* ── Helpers ─────────────────────────────────────────────────────────────── */

void Database::exec(const std::string &sql)
{
    char *err = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "unknown";
        sqlite3_free(err);
        throw std::runtime_error("SQL error: " + msg + " [" + sql + "]");
    }
}

std::string Database::nowStr()
{
    char buf[32];
    time_t now = time(nullptr);
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S",
                  std::localtime(&now));
    return buf;
}

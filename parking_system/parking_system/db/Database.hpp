/**
 * @file Database.hpp
 * @brief C++ RAII wrapper around a SQLite3 database handle.
 *
 * Provides typed methods for the two operations the parking system needs:
 *   - Upsert / delete city prices.
 *   - Insert a completed parking record.
 *
 * All SQL is executed via prepared statements for safety and performance.
 * The constructor creates the schema if it does not already exist.
 */

#pragma once

#include <string>
#include <stdexcept>
#include <sqlite3.h>
#include "../common/common.h"

class Database {
public:
    /**
     * @brief Open (or create) the SQLite database file and create schema.
     * @param path  File path for the SQLite database.
     * @throws std::runtime_error on open or schema failure.
     */
    explicit Database(const std::string &path);

    ~Database();

    /* Non-copyable */
    Database(const Database &)            = delete;
    Database &operator=(const Database &) = delete;

    /**
     * @brief Insert or replace a city price row.
     * @param city          City name (unique key).
     * @param pricePerMin   Price per minute.
     */
    void upsertPrice(const std::string &city, double pricePerMin);

    /**
     * @brief Delete a city price row.
     * @param city  City to remove.
     */
    void deletePrice(const std::string &city);

    /**
     * @brief Persist a completed parking session.
     * @param rec  Fully populated record (complete == 1).
     */
    void insertRecord(const parking_record_t &rec);

    /**
     * @brief Execute multiple operations atomically.
     *        Usage: db.transaction([&]{ db.upsert(...); db.upsert(...); });
     */
    template<typename Fn>
    void transaction(Fn &&fn)
    {
        exec("BEGIN;");
        try {
            fn();
            exec("COMMIT;");
        } catch (...) {
            exec("ROLLBACK;");
            throw;
        }
    }

private:
    sqlite3 *db_ = nullptr;

    void exec(const std::string &sql);
    void createSchema();
    static std::string nowStr();
};

#ifndef LOG_T_HPP
#define LOG_T_HPP

#include <ctime>
#include <fstream>
#include <string>

/**
 * @file log_t.hpp
 * @brief Минимальный логгер для TUI.
 *
 * Открывается в append-режиме, каждая запись — с timestamp-префиксом
 * и flush, чтобы при падении/обрыве последние строки не терялись.
 */
class log_t {
    private:
        std::string path_;
        std::ofstream out_;
        bool enabled_ = false;

    public:
        log_t() = default;
        log_t(const log_t&) = delete;
        log_t& operator=(const log_t&) = delete;
        ~log_t() { close(); }

        /**
         * @brief Открыть лог в append-режиме.
         * @return @c 0x00 при успехе, @c 0x01 если файл не открылся.
         */
        int open(const std::string& path) {
            path_ = path;
            out_.open(path, std::ios::app);
            enabled_ = static_cast<bool>(out_);
            return enabled_ ? 0x00 : 0x01;
        }

        void close() {
            if (out_.is_open()) out_.close();
            enabled_ = false;
        }

        bool is_open() const { return enabled_; }
        const std::string& path() const { return path_; }

        /// @brief Записать строку с timestamp-префиксом (с flush).
        void write(const std::string& msg) {
            if (!enabled_) return;

            std::time_t t = std::time(nullptr);
            char buf[32] = {0};
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S",
                          std::localtime(&t));

            out_ << "[" << buf << "] " << msg << '\n';
            out_.flush();
        }

        /// @brief Записать ошибку (с префиксом ERROR).
        void error(const std::string& msg) { write("[ERROR] " + msg); }
};

#endif // LOG_T_HPP
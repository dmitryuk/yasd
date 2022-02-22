/*
  +----------------------------------------------------------------------+
  | Yasd                                                                 |
  +----------------------------------------------------------------------+
  | This source file is subject to version 2.0 of the Apache license,    |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.apache.org/licenses/LICENSE-2.0.html                      |
  | If you did not receive a copy of the Apache2.0 license and are unable|
  | to obtain it through the world-wide-web, please send a note to       |
  | license@swoole.com so we can mail you a copy immediately.            |
  +----------------------------------------------------------------------+
  | Author: codinghuang  <codinghuang@qq.com>                            |
  +----------------------------------------------------------------------+
*/

#include <libgen.h>
#include <iostream>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/filesystem.hpp>

#include "./php_yasd_cxx.h"
#include "include/context.h"
#include "include/cmder_debugger.h"
#include "include/util.h"
#include "include/global.h"
#include "include/source_reader.h"

namespace yasd {

CmderDebugger::CmderDebugger() {}

void CmderDebugger::init() {
    int status = -1;
    std::string cmd;

    show_welcome_info();

    reload_cache_breakpoint();
    register_cmd_handler();

    do {
        cmd = get_next_cmd();
        if (cmd == "") {
            yasd::util::printfln_info(yasd::Color::YASD_ECHO_RED, "please input cmd!");
            continue;
        }
        status = execute_cmd();
    } while (status != yasd::DebuggerModeBase::status::NEXT_OPLINE);
}

void CmderDebugger::handle_request(const char *filename, int lineno) {
    int status = -1;
    std::string cmd;
    yasd::SourceReader reader(filename);
    std::vector<std::string> exploded_cmd;

    boost::split(exploded_cmd, last_cmd, boost::is_any_of(" "), boost::token_compress_on);

    if (get_full_name(exploded_cmd[0]) == "run" || get_full_name(exploded_cmd[0]) == "continue") {
        yasd::util::printf_info(yasd::Color::YASD_ECHO_MAGENTA, "stop because of breakpoint ");
    } else {
        yasd::util::printf_info(
            yasd::Color::YASD_ECHO_MAGENTA, "stop because of %s ", get_full_name(exploded_cmd[0]).c_str());
    }

    reader.show_contents(lineno, get_listsize(), true, true);

    do {
        global->do_next = false;
        global->do_step = false;
        global->do_finish = false;

        cmd = get_next_cmd();
        if (cmd == "") {
            yasd::util::printfln_info(yasd::Color::YASD_ECHO_RED, "please input cmd!");
            continue;
        }
        status = execute_cmd();
    } while (status != yasd::DebuggerModeBase::status::NEXT_OPLINE);
}

void CmderDebugger::handle_stop() {}

CmderDebugger::~CmderDebugger() {}

std::string CmderDebugger::get_next_cmd() {
    std::string tmp;

    std::cout << "> ";
    getline(std::cin, tmp);
    boost::algorithm::trim(tmp);
    if (tmp == "") {
        return last_cmd;
    }
    last_cmd = tmp;
    return last_cmd;
}

int CmderDebugger::parse_run_cmd() {
    global->is_running = true;

    return NEXT_OPLINE;
}

int CmderDebugger::parse_breakpoint_cmd() {
    int lineno;
    std::string filename;
    std::vector<std::string> exploded_cmd;

    boost::split(exploded_cmd, last_cmd, boost::is_any_of(" "), boost::token_compress_on);

    // breakpoint in current file with lineno
    if (exploded_cmd.size() == 2) {
        filename = yasd::util::execution::get_filename();

        if (!yasd::util::string::is_integer(exploded_cmd[1].c_str())) {
            yasd::util::printfln_info(YASD_ECHO_RED, "please set the correct lineno!");
            return RECV_CMD_AGAIN;
        }
        lineno = atoi(exploded_cmd[1].c_str());
    } else if (exploded_cmd.size() == 3) {
        filename = exploded_cmd[1];

        if (!yasd::util::string::is_integer(exploded_cmd[2].c_str())) {
            yasd::util::printfln_info(YASD_ECHO_RED, "please set the correct lineno!");
            return RECV_CMD_AGAIN;
        }
        lineno = atoi(exploded_cmd[2].c_str());
    } else {
        yasd::util::printfln_info(YASD_ECHO_RED, "use set breakpoint cmd error!");
        return RECV_CMD_AGAIN;
    }

    auto iter = global->breakpoints->find(filename);

    if (iter != global->breakpoints->end()) {
        iter->second.insert(lineno);
    } else {
        std::set<int> lineno_set;
        lineno_set.insert(lineno);
        global->breakpoints->insert(std::make_pair(filename, lineno_set));
    }

    cache_breakpoint(filename, lineno);

    yasd::util::printfln_info(yasd::Color::YASD_ECHO_GREEN, "set breakpoint at %s:%d", filename.c_str(), lineno);

    return RECV_CMD_AGAIN;
}

int CmderDebugger::parse_delete_breakpoint_cmd() {
    std::string filename;
    std::vector<std::string> exploded_cmd;

    boost::split(exploded_cmd, last_cmd, boost::is_any_of(" "), boost::token_compress_on);

    if (exploded_cmd.size() < 2) {
        yasd::util::printfln_info(YASD_ECHO_RED, "please input the breakpoint number!");
        return RECV_CMD_AGAIN;
    }

    int number = atoi(exploded_cmd[1].c_str());
    int i = 1;
    for (auto breakpoints_iter = global->breakpoints->begin(); breakpoints_iter != global->breakpoints->end(); breakpoints_iter++) {
        for (auto linenos_iter = breakpoints_iter->second.begin(); linenos_iter != breakpoints_iter->second.end(); linenos_iter++) {
            if (i++ == number) {
                breakpoints_iter->second.erase(linenos_iter);
                yasd::util::printfln_info(yasd::Color::YASD_ECHO_GREEN, "delete breakpoint at %d: %s:%d", i - 1, breakpoints_iter->first.c_str(), *linenos_iter);
                break;
            }
        }
    }

    return RECV_CMD_AGAIN;
}

void CmderDebugger::show_info_cmd_help() {
    yasd::util::printfln_info(yasd::Color::YASD_ECHO_GREEN, "List of info subcommands:\n");
    yasd::util::printfln_info(yasd::Color::YASD_ECHO_GREEN, "info breakpoints -- IDs of currently known checkpoints");
    yasd::util::printfln_info(yasd::Color::YASD_ECHO_GREEN, "info watchpoints -- Status of specified watchpoints (all watchpoints if no argument)");
}

void CmderDebugger::show_breakpoints() {
    int number = 1;
    for (auto breakpoints_iter = global->breakpoints->begin(); breakpoints_iter != global->breakpoints->end(); breakpoints_iter++) {
        for (auto linenos_iter = breakpoints_iter->second.begin(); linenos_iter != breakpoints_iter->second.end(); linenos_iter++) {
            yasd::util::printfln_info(yasd::Color::YASD_ECHO_GREEN, "%d: %s:%d", number++, breakpoints_iter->first.c_str(), *linenos_iter);
        }
    }
}

void CmderDebugger::show_watchpoints() {
    int number = 1;
    for (auto variable_changes_iter = global->watchPoints.variable_change_watchpoint.begin(); variable_changes_iter != global->watchPoints.variable_change_watchpoint.end(); variable_changes_iter++) {
        yasd::util::printfln_info(yasd::Color::YASD_ECHO_GREEN, "%s:", yasd::util::execution::get_function_name(variable_changes_iter->first));

        for (auto conditions_iter : *variable_changes_iter->second) {
            yasd::util::printfln_info(yasd::Color::YASD_ECHO_GREEN, "%d: $%s", number++, conditions_iter.first.c_str());
        }
    }
    for (auto condition_watchpoints_iter = global->watchPoints.condition_watchpoint.begin(); condition_watchpoints_iter != global->watchPoints.condition_watchpoint.end(); condition_watchpoints_iter++) {
        yasd::util::printfln_info(yasd::Color::YASD_ECHO_GREEN, "%s:", yasd::util::execution::get_function_name(condition_watchpoints_iter->first));

        for (auto conditions_iter : *condition_watchpoints_iter->second) {
            yasd::util::printfln_info(yasd::Color::YASD_ECHO_GREEN, "%d: %s", number++, conditions_iter.c_str());
        }
    }
}

int CmderDebugger::parse_info_cmd() {
    std::vector<std::string> exploded_cmd;

    boost::split(exploded_cmd, last_cmd, boost::is_any_of(" "), boost::token_compress_on);

    if (exploded_cmd.size() < 2) {
        yasd::util::printfln_info(yasd::Color::YASD_ECHO_RED, "\"info\" must be followed by the name of an info command.");

        show_info_cmd_help();
        return RECV_CMD_AGAIN;
    }

    std::string type = exploded_cmd[1];

    if (type == "breakpoints") {
        show_breakpoints();
        return RECV_CMD_AGAIN;
    } else if (type == "watchpoints") {
        show_watchpoints();
        return RECV_CMD_AGAIN;
    }

    yasd::util::printfln_info(yasd::Color::YASD_ECHO_RED, "error info name.");

    show_info_cmd_help();

    return RECV_CMD_AGAIN;
}

int CmderDebugger::parse_level_cmd() {
    yasd::Context *context = global->get_current_context();

    std::cout << "in coroutine: " << context->cid << "next coroutine: " << global->next_cid
              << ", level: " << context->level << ", next level: " << context->next_level << std::endl;

    return RECV_CMD_AGAIN;
}

int CmderDebugger::parse_backtrace_cmd() {
    yasd::Context *context = global->get_current_context();

    yasd::util::printfln_info(
        YASD_ECHO_GREEN, "%s:%d", yasd::util::execution::get_filename(), yasd::util::execution::get_file_lineno());

    for (auto iter = context->strace->rbegin(); iter != context->strace->rend(); ++iter) {
        yasd::util::printfln_info(YASD_ECHO_GREEN, "%s:%d", (*iter)->filename.c_str(), (*iter)->lineno);
    }

    return RECV_CMD_AGAIN;
}

int CmderDebugger::parse_continue_cmd() {
    return NEXT_OPLINE;
}

int CmderDebugger::parse_quit_cmd() {
    yasd::util::printfln_info(YASD_ECHO_RED, "quit!");
    exit(255);

    return FAILED;
}

int CmderDebugger::parse_print_cmd() {
    std::vector<std::string> exploded_cmd;

    boost::split(exploded_cmd, last_cmd, boost::is_any_of(" "), boost::token_compress_on);

    std::string var_name = exploded_cmd[1];

    yasd::util::variable::print_var(var_name.erase(0, 1));
    global->do_next = true;

    return RECV_CMD_AGAIN;
}

int CmderDebugger::parse_list_cmd() {
    int lineno = last_list_lineno;
    const char *filename = yasd::util::execution::get_filename();
    yasd::SourceReader reader(filename);
    std::vector<std::string> exploded_cmd;

    boost::split(exploded_cmd, last_cmd, boost::is_any_of(" "), boost::token_compress_on);

    // list lineno or list -
    if (exploded_cmd.size() == 2) {
        if (exploded_cmd[1] == "-") {
            lineno = last_list_lineno - listsize;
        } else {
            lineno = atoi(exploded_cmd[1].c_str());
            lineno = last_list_lineno + listsize;
        }
        last_list_lineno = lineno;
    } else {
        // list
        lineno = last_list_lineno;
        last_list_lineno = lineno + listsize;
    }

    reader.show_contents(lineno, get_listsize());
    return RECV_CMD_AGAIN;
}

int CmderDebugger::parse_set_cmd() {
    std::vector<std::string> exploded_cmd;

    boost::split(exploded_cmd, last_cmd, boost::is_any_of(" "), boost::token_compress_on);

    if (exploded_cmd[1] == "listsize") {
        listsize = atoi(exploded_cmd[2].c_str());
    }
    return RECV_CMD_AGAIN;
}

int CmderDebugger::parse_watch_cmd() {
    zend_function *func = EG(current_execute_data)->func;
    std::string var_name;
    std::string condition;
    yasd::WatchPointElement element;
    std::vector<std::string> exploded_cmd;

    boost::split(exploded_cmd, last_cmd, boost::is_any_of(" "), boost::token_compress_on);
    if (exploded_cmd.size() < 2) {
        yasd::util::printfln_info(yasd::Color::YASD_ECHO_RED, "you should set watch point");
        return RECV_CMD_AGAIN;
    } else if (exploded_cmd.size() == 2) { // variable change watch point
        var_name = exploded_cmd[1];
        var_name.erase(0, 1);

        zval *old_var = yasd::util::variable::find_variable(var_name);

        if (!old_var) {
            zval tmp;
            old_var = &tmp;
            ZVAL_UNDEF(old_var);
        }

        element.old_var = *old_var;

        auto iter = global->watchPoints.variable_change_watchpoint.find(func);
        if (iter == global->watchPoints.variable_change_watchpoint.end()) {
            VARIABLE_CHANGE_WATCH_POINT *watchpoint = new VARIABLE_CHANGE_WATCH_POINT();
            watchpoint->insert(std::make_pair(var_name, element));
            global->watchPoints.variable_change_watchpoint.insert(std::make_pair(func, watchpoint));
        } else {
            iter->second->insert(std::make_pair(var_name, element));
        }

        yasd::util::printfln_info(yasd::Color::YASD_ECHO_GREEN, "watching variable $%s", var_name.c_str());
    } else { // condition watch point
        const auto equals_idx = last_cmd.find_first_of(" ");
        std::string condition = last_cmd.substr(equals_idx + 1);

        auto iter = global->watchPoints.condition_watchpoint.find(func);
        if (iter == global->watchPoints.condition_watchpoint.end()) {
            CONDITION_WATCH_POINT *watchpoint = new CONDITION_WATCH_POINT();
            watchpoint->insert(condition);
            global->watchPoints.condition_watchpoint.insert(std::make_pair(func, watchpoint));
        } else {
            iter->second->insert(condition);
        }

        yasd::util::printfln_info(yasd::Color::YASD_ECHO_GREEN, "watching condition %s", condition.c_str());
    }

    return RECV_CMD_AGAIN;
}

int CmderDebugger::parse_unwatch_cmd() {
    std::vector<std::string> exploded_cmd;

    boost::split(exploded_cmd, last_cmd, boost::is_any_of(" "), boost::token_compress_on);

    if (exploded_cmd.size() < 2) {
        yasd::util::printfln_info(YASD_ECHO_RED, "please input the breakpoint number!");
        return RECV_CMD_AGAIN;
    }

    int i = atoi(exploded_cmd[1].c_str());
    int number = 1;
    for (auto variable_changes_iter = global->watchPoints.variable_change_watchpoint.begin(); variable_changes_iter != global->watchPoints.variable_change_watchpoint.end(); variable_changes_iter++) {
        for (auto conditions_iter : *variable_changes_iter->second) {
            if (number++ == i) {
                variable_changes_iter->second->erase(conditions_iter.first);
                yasd::util::printfln_info(yasd::Color::YASD_ECHO_GREEN, "unwatch variable $%s", conditions_iter.first.c_str());
                if (variable_changes_iter->second->empty()) {
                    global->watchPoints.variable_change_watchpoint.erase(variable_changes_iter->first);
                }
                return RECV_CMD_AGAIN;
            }
        }
    }
    for (auto condition_watchpoints_iter = global->watchPoints.condition_watchpoint.begin(); condition_watchpoints_iter != global->watchPoints.condition_watchpoint.end(); condition_watchpoints_iter++) {
        for (auto conditions_iter : *condition_watchpoints_iter->second) {
            if (number++ == i) {
                condition_watchpoints_iter->second->erase(conditions_iter);
                yasd::util::printfln_info(yasd::Color::YASD_ECHO_GREEN, "unwatch condition %s", conditions_iter.c_str());
                if (condition_watchpoints_iter->second->empty()) {
                    global->watchPoints.condition_watchpoint.erase(condition_watchpoints_iter->first);
                }
                return RECV_CMD_AGAIN;
            }
        }
    }

    return RECV_CMD_AGAIN;
}

bool CmderDebugger::is_disable_cmd(std::string cmd) {
    // the command in the condition is allowed to execute in non-run state
    if (get_full_name(cmd) != "run" && get_full_name(cmd) != "b" && get_full_name(cmd) != "quit" &&
        get_full_name(cmd) != "info" && get_full_name(cmd) != "delete" && get_full_name(cmd) != "list" &&
        get_full_name(cmd) != "set") {
        return true;
    }

    return false;
}

std::string CmderDebugger::get_full_name(std::string sub_cmd) {
    for (auto &&kv : handlers) {
        if (yasd::util::string::is_substring(sub_cmd, kv.first)) {
            return kv.first;
        }
    }
    return "unknown cmd";
}

void CmderDebugger::show_welcome_info() {
    yasd::util::printfln_info(YASD_ECHO_GREEN, "[Welcome to yasd, the Swoole debugger]");
    yasd::util::printfln_info(YASD_ECHO_GREEN, "[You can set breakpoint now]");
}

std::string CmderDebugger::get_breakpoint_cache_filename() {
    return std::string(YASD_G(breakpoints_file));
}

void CmderDebugger::cache_breakpoint(std::string filename, int lineno) {
    std::ofstream file;
    std::string cache_filename_path = get_breakpoint_cache_filename();

    if (cache_filename_path == "") {
        return;
    }

    file.open(cache_filename_path, std::ios_base::app);
    file << filename + ":" + std::to_string(lineno) + "\n";
    file.close();
}

void CmderDebugger::reload_cache_breakpoint() {
    std::string content;
    std::string cache_filename_path = get_breakpoint_cache_filename();

    if (cache_filename_path == "") {
        return;
    }

    char *dir_name = dirname(const_cast<char *>(cache_filename_path.c_str()));

    // if path does not contain a '/', dirname() shall return a pointer to the string "."
    if (*dir_name != '.') {  // it means dir_name is directory
        boost::filesystem::create_directories(dir_name);
    }

    std::fstream file(cache_filename_path);
    std::string filename;
    int lineno;

    while (getline(file, content)) {
        std::vector<std::string> exploded_content;

        boost::split(exploded_content, content, boost::is_any_of(":"), boost::token_compress_on);
        if (exploded_content.size() != 2) {
            continue;
        }

        filename = exploded_content[0];
        lineno = atoi(exploded_content[1].c_str());

        auto iter = global->breakpoints->find(filename);

        yasd::util::printfln_info(yasd::Color::YASD_ECHO_GREEN, "reload breakpoint at %s:%d", filename.c_str(), lineno);

        if (iter != global->breakpoints->end()) {
            iter->second.insert(lineno);
        } else {
            std::set<int> lineno_set;
            lineno_set.insert(lineno);
            global->breakpoints->insert(std::make_pair(filename, lineno_set));
        }
    }
}

int CmderDebugger::execute_cmd() {
    std::vector<std::string> exploded_cmd;

    boost::split(exploded_cmd, last_cmd, boost::is_any_of(" "), boost::token_compress_on);

    if (!global->is_running) {
        if (is_disable_cmd(exploded_cmd[0])) {
            yasd::util::printfln_info(YASD_ECHO_RED, "program is not running!");
            return RECV_CMD_AGAIN;
        }
    }

    auto handler = find_cmd_handler(exploded_cmd[0]);
    if (!handler) {
        return FAILED;
    }

    return handler();
}

void CmderDebugger::register_cmd_handler() {
    handlers.emplace_back(std::make_pair("run", std::bind(&CmderDebugger::parse_run_cmd, this)));
    handlers.emplace_back(std::make_pair("b", std::bind(&CmderDebugger::parse_breakpoint_cmd, this)));
    handlers.emplace_back(std::make_pair("bt", std::bind(&CmderDebugger::parse_backtrace_cmd, this)));
    handlers.emplace_back(std::make_pair("delete", std::bind(&CmderDebugger::parse_delete_breakpoint_cmd, this)));
    handlers.emplace_back(std::make_pair("info", std::bind(&CmderDebugger::parse_info_cmd, this)));
    handlers.emplace_back(std::make_pair("step", std::bind(&CmderDebugger::parse_step_into_cmd, this)));
    handlers.emplace_back(std::make_pair("list", std::bind(&CmderDebugger::parse_list_cmd, this)));
    handlers.emplace_back(std::make_pair("next", std::bind(&CmderDebugger::parse_step_over_cmd, this)));
    handlers.emplace_back(std::make_pair("continue", std::bind(&CmderDebugger::parse_continue_cmd, this)));
    handlers.emplace_back(std::make_pair("quit", std::bind(&CmderDebugger::parse_quit_cmd, this)));
    handlers.emplace_back(std::make_pair("print", std::bind(&CmderDebugger::parse_print_cmd, this)));
    handlers.emplace_back(std::make_pair("finish", std::bind(&CmderDebugger::parse_step_out_cmd, this)));
    handlers.emplace_back(std::make_pair("set", std::bind(&CmderDebugger::parse_set_cmd, this)));
    handlers.emplace_back(std::make_pair("level", std::bind(&CmderDebugger::parse_level_cmd, this)));
    handlers.emplace_back(std::make_pair("watch", std::bind(&CmderDebugger::parse_watch_cmd, this)));
    handlers.emplace_back(std::make_pair("unwatch", std::bind(&CmderDebugger::parse_unwatch_cmd, this)));
}

std::function<int()> CmderDebugger::find_cmd_handler(std::string cmd) {
    for (auto &&kv : handlers) {
        if (kv.first == get_full_name(cmd)) {
            return kv.second;
        }
    }
    return nullptr;
}
}  // namespace yasd

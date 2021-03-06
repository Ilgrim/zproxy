/*
 *    Zevenet zproxy Load Balancer Software License
 *    This file is part of the Zevenet zproxy Load Balancer software package.
 *
 *    Copyright (C) 2019-today ZEVENET SL, Sevilla (Spain)
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU Affero General Public License as
 *    published by the Free Software Foundation, either version 3 of the
 *    License, or any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#pragma once

#include "../debug/logger.h"
#include "system.h"
#include <csignal>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <string>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

class Environment {

public:
  static bool setFileUserName(const std::string &user_name,
                              const std::string &file_name) {

    if (!user_name.empty()) {
      struct passwd *pw;

      if ((pw = ::getpwnam(user_name.c_str())) == nullptr) {
        Logger::logmsg(LOG_ERR, "no such user %s - aborted", user_name.c_str());
        return false;
      }
      if (::chown(file_name.c_str(), pw->pw_uid, -1)) {
        Logger::logmsg(LOG_ERR, "chown error on control socket - aborted (%s)",
                      strerror(errno));
        return false;
      }
      return true;
    }
    return false;
  }

  static bool setFileGroupName(const std::string &group_name,
                               const std::string &file_name) {
    if (!group_name.empty()) {
      struct group *gr;
      if ((gr = ::getgrnam(group_name.c_str())) == nullptr) {
        Logger::logmsg(LOG_ERR, "no such group %s - aborted",
                      group_name.c_str());
        return false;
      }
      if (::chown(file_name.c_str(), -1, gr->gr_gid)) {
        Logger::logmsg(LOG_ERR, "chown error on control socket - aborted (%s)",
                      strerror(errno));
        return false;
      }
      return true;
    }
    return false;
  }

  static bool setFileUserMode(long user_mode, const std::string &file_name) {
    if (::chmod(file_name.c_str(), user_mode)) {
      Logger::logmsg(LOG_ERR, "chmod error on control socket - aborted (%s)",
                    strerror(errno));
      return false;
    }
    return true;
  }

  static bool setUid(const std::string &user) {
    if (!user.empty()) {
      struct passwd *pw;
      if ((pw = ::getpwnam(user.c_str())) == nullptr) {
        Logger::logmsg(LOG_ERR, "no such user %s - aborted", user.c_str());
        return false;
      }
      auto user_id = pw->pw_uid;
      if (::setuid(user_id) || seteuid(user_id)) {
        Logger::logmsg(LOG_ERR, "setuid: %s - aborted", strerror(errno));
        return false;
      }
      return true;
    }
    return false;
  }

  static bool setGid(const std::string &group_name) {
    if (!group_name.empty()) {
      struct group *gr;
      if ((gr = ::getgrnam(group_name.c_str())) == nullptr) {
        Logger::logmsg(LOG_ERR, "no such group %s - aborted",
                      group_name.c_str());
        return false;
      }
      auto group_id = gr->gr_gid;
      if (::setgid(group_id) || setegid(group_id)) {
        Logger::logmsg(LOG_ERR, "setgid: %s - aborted", strerror(errno));
        return false;
      }
      return true;
    }
    return false;
  }

  static bool createPidFile(const std::string &pid_file_name, int pid = -1) {
    auto pid_file_hl = ::fopen(pid_file_name.c_str(), "wt");
    if (pid_file_hl != nullptr) {
      fprintf(pid_file_hl, "%d\n", pid != -1 ? pid : getpid());
      fclose(pid_file_hl);
      return true;
    } else
      logmsg(LOG_NOTICE, "Create \"%s\": %s", pid_file_name.c_str(),
             strerror(errno));
    return false;
  }

  static bool removePidFile(const std::string &pid_file_name) {
    struct stat info;
    if (lstat(pid_file_name.data(), &info) != 0) return false;
    if (!S_ISREG(info.st_mode)) return false;
    if (info.st_uid != getuid()) return false;
    if (info.st_size > static_cast<int>(sizeof("65535\r\n"))) return false;
    unlink(pid_file_name.data());
    return true;
  }

  static bool setChrootRoot(const std::string &chroot_path) {
    if (!chroot_path.empty()) {
      if (::chroot(chroot_path.c_str())) {
        Logger::logmsg(LOG_ERR, "chroot: %s - aborted", strerror(errno));
        return false;
      }
      if (chdir("/")) {
        Logger::logmsg(LOG_ERR, "chroot/chdir: %s - aborted", strerror(errno));
        return false;
      }
      return true;
    }
    return false;
  }

  // Increase num file descriptor ulimit
  static bool setUlimitData() {

//    Logger::logmsg(LOG_DEBUG,"System info:");
//    Logger::logmsg(LOG_DEBUG,"\tL1 Data cache size: %lu", SystemInfo::data()->getL1DataCacheSize());
//    Logger::logmsg(LOG_DEBUG,"\t\tCache line size: %lu",SystemInfo::data()->getL1DataCacheLineSize());
//    Logger::logmsg(LOG_DEBUG,"\tL2 Cache size: %lu",SystemInfo::data()->getL2DataCacheSize());
//    Logger::logmsg(LOG_DEBUG,"\t\tCache line size: %lu" ,SystemInfo::data()->getL2DataCacheLineSize());
    rlimit r{};
    ::getrlimit(RLIMIT_NOFILE, &r);
//    Logger::logmsg(LOG_DEBUG,"\tRLIMIT_NOFILE\tCurrent %lu" , r.rlim_cur);
//    Logger::logmsg(LOG_DEBUG,"\tRLIMIT_NOFILE\tMaximum %lu" , ::sysconf(_SC_OPEN_MAX));
    if (r.rlim_cur != r.rlim_max) {
      r.rlim_cur = r.rlim_max;
      if (setrlimit(RLIMIT_NOFILE, &r) == -1) {
        Logger::logmsg(LOG_ERR, "\tsetrlimit failed ");
        return false;
      }
    }
    ::getrlimit(RLIMIT_NOFILE, &r);
//    Logger::LogInfo("\tRLIMIT_NOFILE\tSetCurrent " + std::to_string(r.rlim_cur), LOG_DEBUG);
    return true;
  }

 static void redirectLogOutput(std::string name, std::string chroot_path,
                         std::string outfile, std::string errfile,
                         std::string infile) {
    if (chroot_path.empty()) {
      chroot_path = "/";
    }
    if (name.empty()) {
      name = "zproxy";
    }
    if (infile.empty()) {
      infile = "/dev/null";
    }
    if (outfile.empty()) {
      outfile = "/dev/null";
    }
    if (errfile.empty()) {
      errfile = "/dev/null";
    }
    // new file permissions
    umask(0);
    // change to path directory
    chdir(chroot_path.c_str());
    // Carefull Close all open file descriptors
    //  int fd;
    //  for (fd = ::sysconf(_SC_OPEN_MAX); fd > 0; --fd) {
    //    close(fd);
    //  }
    // reopen stdin, stdout, stderr
    stdin = fopen(infile.c_str(), "r");
    stdout = fopen(outfile.c_str(), "w+");
    stderr = fopen(errfile.c_str(), "w+");
  }

 static bool daemonize() {
     pid_t child;
     if ((child = fork()) < 0) {
       Logger::logmsg(LOG_ERR, "error: failed fork");
         return false;
     }
     if (child != 0) { // parent
         _exit(EXIT_SUCCESS); //avoid triggering atexit() processing using _exit()
     }

     /* Don't hold files open. */
     ::close(STDIN_FILENO);
     ::close(STDOUT_FILENO);
     ::close(STDERR_FILENO);

     /* Many routines write to stderr; that can cause chaos if used
     * for something else, so set it here. */
     if (::open("/dev/null", O_WRONLY) != 0)
         return false;
     if (::dup2(0, STDERR_FILENO) != STDERR_FILENO)
         return false;
     ::close(0);

     //  become session leader so SIGTERM do not affect child
     if (setsid() ==  static_cast<pid_t>(-1)) {
         std::cerr << "error: failed setsid\n";
         return false;
     }

     /* Move off any mount points we might be in. */
     if (chdir("/") != 0)
         return false;

     // catch/ignore signals
     signal(SIGCHLD, SIG_IGN);
     signal(SIGHUP, SIG_IGN);
//     // fork second time since parent exits
//     if ((child = fork()) < 0) { // failed fork
//         std::cerr << "error: failed fork\n";
//         exit(EXIT_FAILURE);
//     }
//     if (child > 0) {
//         exit(EXIT_SUCCESS);
//     }
     /* Discard our parent's umask. */
     umask(0);
     return true;
 }};

#include "host.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unordered_map>

#include "user_error.h"

namespace zion {

namespace {
std::unordered_map<std::string, int> host_values;
}

#define V(x) host_values[#x] = x

void init_host() {
  // NB: this way of grabbing these values means that this compiler must be
  // built on the target platform. This can be changed in the future, but this
  // is the simplest approach for now.
  V(O_RDONLY);
  V(O_WRONLY);
  V(O_RDWR);
  V(O_ACCMODE);
  V(O_CREAT);
  V(O_EXCL);
  V(O_NOCTTY);
  V(O_TRUNC);
  V(O_APPEND);
  V(O_NONBLOCK);
  V(S_IRWXU);
  V(S_IRUSR);
  V(S_IWUSR);
  V(S_IXUSR);
  V(S_IRWXG);
  V(S_IRGRP);
  V(S_IWGRP);
  V(S_IXGRP);
  V(S_IRWXO);
  V(S_IROTH);
  V(S_IWOTH);
  V(S_IXOTH);
  V(SEEK_SET);
  V(SEEK_CUR);
  V(SEEK_END);
  V(EPERM);
  V(ENOENT);
  V(ESRCH);
  V(EINTR);
  V(EIO);
  V(ENXIO);
  V(E2BIG);
  V(ENOEXEC);
  V(EBADF);
  V(ECHILD);
  V(EAGAIN);
  V(ENOMEM);
  V(EACCES);
  V(EFAULT);
  V(ENOTBLK);
  V(EBUSY);
  V(EEXIST);
  V(EXDEV);
  V(ENODEV);
  V(ENOTDIR);
  V(EISDIR);
  V(EINVAL);
  V(ENFILE);
  V(EMFILE);
  V(ENOTTY);
  V(ETXTBSY);
  V(EFBIG);
  V(ENOSPC);
  V(ESPIPE);
  V(EROFS);
  V(EMLINK);
  V(EPIPE);
  V(EDOM);
  V(ERANGE);
  V(EDEADLK);
  V(ENAMETOOLONG);
  V(ENOLCK);
  V(ENOSYS);
  V(ENOTEMPTY);
  V(ELOOP);
  V(EWOULDBLOCK);
  V(ENOMSG);
  V(EIDRM);
  // V(ECHRNG);
  // V(EL2NSYNC);
  // V(EL3HLT);
  // V(EL3RST);
  // V(ELNRNG);
  // V(EUNATCH);
  // V(ENOCSI);
  // V(EL2HLT);
  // V(EBADE);
  // V(EBADR);
  // V(EXFULL);
  // V(ENOANO);
  // V(EBADRQC);
  // V(EBADSLT);
  // V(EDEADLOCK);
  // V(EBFONT);
  V(ENOSTR);
  V(ENODATA);
  V(ETIME);
  V(ENOSR);
  // V(ENONET);
  // V(ENOPKG);
  V(EREMOTE);
  V(ENOLINK);
  // V(EADV);
  // V(ESRMNT);
  // V(ECOMM);
  V(EPROTO);
  V(EMULTIHOP);
  // V(EDOTDOT);
  V(EBADMSG);
  V(EOVERFLOW);
  // V(ENOTUNIQ);
  // V(EBADFD);
  // V(EREMCHG);
  // V(ELIBACC);
  // V(ELIBBAD);
  // V(ELIBSCN);
  // V(ELIBMAX);
  // V(ELIBEXEC);
  V(EILSEQ);
  // V(ERESTART);
  // V(ESTRPIPE);
  V(EUSERS);
  V(ENOTSOCK);
  V(EDESTADDRREQ);
  V(EMSGSIZE);
  V(EPROTOTYPE);
  V(ENOPROTOOPT);
  V(EPROTONOSUPPORT);
  V(ESOCKTNOSUPPORT);
  V(EOPNOTSUPP);
  V(EPFNOSUPPORT);
  V(EAFNOSUPPORT);
  V(EADDRINUSE);
  V(EADDRNOTAVAIL);
  V(ENETDOWN);
  V(ENETUNREACH);
  V(ENETRESET);
  V(ECONNABORTED);
  V(ECONNRESET);
  V(ENOBUFS);
  V(EISCONN);
  V(ENOTCONN);
  V(ESHUTDOWN);
  V(ETOOMANYREFS);
  V(ETIMEDOUT);
  V(ECONNREFUSED);
  V(EHOSTDOWN);
  V(EHOSTUNREACH);
  V(EALREADY);
  V(EINPROGRESS);
  V(ESTALE);
  // V(EUCLEAN);
  // V(ENOTNAM);
  // V(ENAVAIL);
  // V(EISNAM);
  // V(EREMOTEIO);
  V(EDQUOT);
  // V(ENOMEDIUM);
  // V(EMEDIUMTYPE);
  V(ECANCELED);
  // V(ENOKEY);
  // V(EKEYEXPIRED);
  // V(EKEYREVOKED);
  // V(EKEYREJECTED);
  V(EOWNERDEAD);
  V(ENOTRECOVERABLE);
  // V(ERFKILL);
  // V(EHWPOISON);
  V(SIGHUP);
  V(SIGINT);
  V(SIGQUIT);
  V(SIGILL);
  V(SIGTRAP);
  V(SIGABRT);
  V(SIGIOT);
  V(SIGBUS);
  V(SIGFPE);
  V(SIGKILL);
  V(SIGUSR1);
  V(SIGSEGV);
  V(SIGUSR2);
  V(SIGPIPE);
  V(SIGALRM);
  V(SIGTERM);
  // V(SIGSTKFLT);
  V(SIGCHLD);
  V(SIGCONT);
  V(SIGSTOP);
  V(SIGTSTP);
  V(SIGTTIN);
  V(SIGTTOU);
  V(SIGURG);
  V(SIGXCPU);
  V(SIGXFSZ);
  V(SIGVTALRM);
  V(SIGPROF);
  V(SIGWINCH);
  V(SIGIO);
  // V(SIGPOLL);
  // V(SIGLOST);
  // V(SIGPWR);
  V(SIGSYS);
  // V(SIGUNUSED);
  // V(SIGRTMIN);
  // V(SIGRTMAX);
  V(AF_INET);
  V(AF_INET6);
#ifndef __APPLE__
  V(AF_PACKET);
  V(AF_NETLINK);
#endif
  V(SOCK_STREAM);
  V(SOCK_DGRAM);
}

int get_host_int(Location location, std::string name) {
  if (host_values.count(name) == 0) {
    throw user_error(location, "undefined host value %s", name.c_str());
  }
  return host_values.at(name);
}

} // namespace zion

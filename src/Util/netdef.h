#ifndef __SRC_NET_DEFINE_H__
#define __SRC_NET_DEFINE_H__

#pragma once

#if defined(_WIN32)
#undef FD_SETSIZE
//修改默认64为1024路
#define FD_SETSIZE 1024
#include <winsock2.h>
#pragma comment (lib,"WS2_32")
#else
#include <netinet/in.h>
#endif // defined(_WIN32)

#endif

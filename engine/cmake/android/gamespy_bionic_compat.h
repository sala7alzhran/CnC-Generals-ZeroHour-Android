// GeneralsX @port Android — force-included into the fetched GameSpy SDK's gscommon target.
// bionic has no pthread_cancel (see docs/discoveries/bionic-and-libcxx-gaps.md in the Android
// port). GameSpy calls it only in gsiCancelThread, used to abandon an async DNS lookup for the
// online services; give it the same best-effort no-op as CompatLib's TerminateThread on Android.
#pragma once
#include <pthread.h>

static inline int pthread_cancel(pthread_t thread)
{
	(void)thread;
	return 0;
}

#pragma once
// The device/suspend integration belongs to the event-thread tests. These tests
// run the production stream worker without suspending the application.
struct SyncPoint { void passSecondarySync() {} };
struct RGSSThreadData { SyncPoint syncPoint; };

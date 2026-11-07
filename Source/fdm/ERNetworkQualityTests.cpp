#include "ERNetworkQuality.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERNetworkQualityTest,"EclipseRecoil.Network.QualityAndSyncDeadline",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERNetworkQualityTest::RunTest(const FString&)
{
    FERNetworkQuality Quality;
    TestTrue(TEXT("Network protocol distinguishes an uncommitted new build from the old wire format"),ERNetworkProtocol::BuildSignature(TEXT("same-commit"))!=TEXT("same-commit"));
    for(int32 I=0;I<20;++I)Quality.Observe(.1);
    TestTrue(TEXT("Stable RTT remains at 100 ms"),FMath::IsNearlyEqual(Quality.RoundTripMs,100.,.001));
    TestEqual(TEXT("Stable RTT has zero variation"),Quality.JitterMs,0.);
    Quality.Observe(.3);
    TestTrue(TEXT("Latency spike is visible without replacing history"),Quality.RoundTripMs>100 && Quality.RoundTripMs<300 && Quality.JitterMs>0);
    const int32 Samples=Quality.Samples;
    Quality.Observe(-1);Quality.Observe(5);
    TestEqual(TEXT("Invalid or expired probes cannot pollute telemetry"),Quality.Samples,Samples);
    for(int32 I=0;I<100;++I)Quality.Observe(.1);
    TestTrue(TEXT("Jitter estimate recovers after stable samples"),Quality.JitterMs<.001);

    FERMapSyncDeadline Deadline;Deadline.Reset(10);
    TestFalse(TEXT("Pending client gets a real load window"),Deadline.Expired(69));
    Deadline.Observe(1,65);
    TestFalse(TEXT("Verified progress extends the stall deadline"),Deadline.Expired(100));
    Deadline.Observe(1,100);Deadline.Observe(0,110);
    TestTrue(TEXT("Duplicate or stale progress is not a keepalive"),Deadline.Expired(125));
    Deadline.Reset(200);
    for(uint64 Serial=1;Serial<=29;++Serial)Deadline.Observe(Serial,200+Serial*10);
    TestFalse(TEXT("Continuing progress permits a slow client"),Deadline.Expired(499));
    TestTrue(TEXT("A changing stream cannot extend the absolute deadline forever"),Deadline.Expired(500));
    Deadline.Reset(700);
    TestEqual(TEXT("New map resets progress independently"),Deadline.Progress,uint64(0));
    TestFalse(TEXT("New map has a new deadline"),Deadline.Expired(701));
    return !HasAnyErrors();
}
#endif

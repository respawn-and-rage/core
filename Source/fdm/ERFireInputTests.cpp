#include "ERFireInput.h"
#include "ERMovement.h"
#include "ERShotTrace.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERFireInputTest,"EclipseRecoil.Network.FireInput",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERFireInputTest::RunTest(const FString&)
{
    FERFireInputState Input;bool Pressed=false;
    TestTrue(TEXT("New press starts"),Input.Receive(1,true,Pressed) && Pressed && Input.Held);
    TestFalse(TEXT("Retransmitted held move never fires twice"),Input.Receive(1,true,Pressed));
    TestTrue(TEXT("A later move recovers a lost release"),Input.Receive(1,false,Pressed) && !Pressed && !Input.Held);
    TestFalse(TEXT("Old input cannot reopen released trigger"),Input.Receive(0,true,Pressed));
    TestTrue(TEXT("Press and release in one move retains a single tap"),Input.Receive(2,false,Pressed) && Pressed && !Input.Held);
    TestFalse(TEXT("Repeated tap state is idempotent"),Input.Receive(2,false,Pressed));
    TestTrue(TEXT("Missing press packet is recovered from persistent counter"),Input.Receive(4,false,Pressed) && Pressed);
    Input.Sequence=MAX_uint16;Input.Held=false;
    TestTrue(TEXT("Sequence wraps without losing a press"),Input.Receive(0,true,Pressed) && Pressed);
    TestFalse(TEXT("Pre-wrap stale press is rejected"),Input.Receive(MAX_uint16,true,Pressed));
    TestFalse(TEXT("New press respects latest known server cooldown"),ERFirePrediction::Ready(.1,0,0,.1,.05,.1));
    TestTrue(TEXT("Delayed accepted snapshot does not postpone automatic cadence"),ERFirePrediction::Ready(.1,.1,1,.1,.05,.1));
    TestFalse(TEXT("Automatic feedback still respects its local cadence"),ERFirePrediction::Ready(.09,.1,1,.3,.05,.1));
    FERShotPresentationWindow Cosmetics;
    TestTrue(TEXT("Prediction produces one presentation"),Cosmetics.Mark(1,1));
    TestFalse(TEXT("Confirmed multicast deduplicates prediction"),Cosmetics.Mark(1,1));
    TestFalse(TEXT("Replicated ack also deduplicates prediction"),Cosmetics.Mark(1,1));
    TestTrue(TEXT("Missing predicted feedback is repaired by confirmation"),Cosmetics.Mark(1,2));
    TestFalse(TEXT("Ack before multicast does not double feedback"),Cosmetics.Mark(1,2));
    TestTrue(TEXT("Next press uses an independent identity"),Cosmetics.Mark(2,1));
    TestTrue(TEXT("Sequence zero is a valid wrapped identity"),Cosmetics.Mark(0,1));
    for(uint16 I=3;I<200;++I)Cosmetics.Mark(I,1);
    TestEqual(TEXT("Presentation window stays bounded"),Cosmetics.Keys.Num(),FERShotPresentationWindow::Capacity);
    TestTrue(TEXT("No unmeasured interpolation bonus"),FMath::IsNearlyEqual(ERShotTrace::RewindSeconds(20,0),.01,1.e-9));
    TestTrue(TEXT("Slower movement view uses only its measured receipt age"),FMath::IsNearlyEqual(ERShotTrace::RewindSeconds(80,.03),.07,1.e-9));
    TestTrue(TEXT("Ping and view age share one 200ms cap"),FMath::IsNearlyEqual(ERShotTrace::RewindSeconds(1000,1),.2,1.e-9));
    TestEqual(TEXT("Negative input does not rewind into future"),ERShotTrace::RewindSeconds(-10,-1),0.);
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERFireWeaponBindingTest,"EclipseRecoil.Network.FireWeaponBinding",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERFireWeaponBindingTest::RunTest(const FString&)
{
    const FERFireWeaponBinding PressA{3,7},SelectedB{4,8},SelectedAAgain{3,9};
    TestTrue(TEXT("Current weapon and selection revision accept the press"),PressA.Matches(3,7));
    TestFalse(TEXT("A delayed A press cannot fire B"),PressA.Matches(SelectedB.Slot,SelectedB.Revision));
    TestFalse(TEXT("Slot mismatch rejects even with equal revision"),PressA.Matches(4,7));
    TestFalse(TEXT("A B A selection cannot revive the old A press"),PressA.Matches(SelectedAAgain.Slot,SelectedAAgain.Revision));
    TestFalse(TEXT("Unresolved inventory slot is never accepted"),FERFireWeaponBinding().Matches(INDEX_NONE,0));
    TestFalse(TEXT("Reserved invalid slot cannot match"),FERFireWeaponBinding().Matches(MAX_uint8,0));
    TestFalse(TEXT("Pre-wrap revision is stale after selection wraps"),(FERFireWeaponBinding{3,MAX_uint16}).Matches(3,0));
    TestTrue(TEXT("Wrapped revision matches by equality"),(FERFireWeaponBinding{3,0}).Matches(3,0));

    FERFireInputState Received;bool Pressed=false;
    TestTrue(TEXT("Recovered lost press is consumed even when bound weapon changed"),Received.Receive(1,false,Pressed) && Pressed);
    TestFalse(TEXT("Recovered press cannot start selected B"),Pressed && PressA.Matches(SelectedB.Slot,SelectedB.Revision));
    TestFalse(TEXT("Retransmission cannot retry rejected press after switching back"),Received.Receive(1,false,Pressed));
    TestTrue(TEXT("Fresh press on selected B remains available"),Received.Receive(2,true,Pressed) && Pressed && SelectedB.Matches(4,8));
    TestTrue(TEXT("Release remains available for the accepted current press"),Received.Receive(2,false,Pressed) && !Pressed && !Received.Held);

    // Exercise the production capture path, including a switch occurring before
    // the press is saved/sent and a release that observes a different selection.
    auto* Movement=NewObject<UERMovement>();
    Movement->SetFireInput(true,PressA);
    const uint16 FirstSequence=Movement->FireInput.Sequence;
    Movement->SetFireInput(true,SelectedB);
    TestTrue(TEXT("Held moves retain the original press binding"),Movement->FireWeapon==PressA);
    TestEqual(TEXT("Held input does not invent another press"),Movement->FireInput.Sequence,FirstSequence);
    Movement->SetFireInput(false,SelectedB);
    TestTrue(TEXT("Release retains A for recovery of its lost press"),Movement->FireWeapon==PressA && !Movement->FireInput.Held);
    Movement->SetFireInput(true,SelectedB);
    TestTrue(TEXT("Only a fresh press captures B and its revision"),Movement->FireWeapon==SelectedB && Movement->FireInput.Sequence==uint16(FirstSequence+1));
    return !HasAnyErrors();
}
#endif

#include "Misc/AutomationTest.h"
#include "ERResolutionPolicy.h"
#include "ERCharacter.h"
#include "ERWounds.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERResolutionTest,"EclipseRecoil.Player.ResolutionHysteresis",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERResolutionTest::RunTest(const FString&)
{
    for(float Dt:{1.f/30,1.f/60,1.f/144})
    {
        FERResolutionPolicy P;
        float SecondsSinceChange=1.f;
        bool ChangesAreSpaced=true;
        auto Update=[&](float Ms)
        {
            const float Before=P.Fraction;
            SecondsSinceChange+=Dt;
            P.Update(Dt,Ms);
            if(P.Fraction!=Before)
            {
                ChangesAreSpaced &= SecondsSinceChange>=.130f;
                SecondsSinceChange=0;
            }
        };
        auto Run=[&](float Seconds,float Ms){for(int32 I=0;I<FMath::RoundToInt(Seconds/Dt);++I)Update(Ms);};
        Run(15,1000.f/59);TestEqual(TEXT("59 FPS stays at full quality"),P.Fraction,1.f);
        Run(1,30);Run(3,1000.f/60);TestEqual(TEXT("Brief dip does not trigger adjustment"),P.Fraction,1.f);
        Run(2,25);TestEqual(TEXT("Sustained load waits three seconds"),P.Fraction,1.f);
        Run(2,25);TestTrue(TEXT("Sustained overload starts bounded adaptation"),P.Fraction<1 && P.Fraction>.8f);
        for(int32 I=0;I<int32(20/Dt);++I)Update(25*P.Fraction*P.Fraction);
        TestTrue(TEXT("Quality converges then holds"),P.Direction==0 && P.Fraction>.75f && P.Fraction<.87f);
        const float Held=P.Fraction;P.HoldSeconds=4;
        Run(3.5,10);TestEqual(TEXT("Cooldown freezes quality even with spare GPU budget"),P.Fraction,Held);
        Run(2,10);TestEqual(TEXT("Recovery waits after cooldown"),P.Fraction,Held);
        Run(3,10);TestTrue(TEXT("Sustained headroom restores quality gradually"),P.Fraction>Held && P.Fraction<=1);
        TestTrue(TEXT("Automatic scale changes are at least 130 ms apart"),ChangesAreSpaced);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERFallTest,"EclipseRecoil.Player.FallDamage",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERFallTest::RunTest(const FString&)
{
    const float Gravity=1764;
    auto Damage=[&](float Height, float Depth = 0)
    {return AERCharacter::FallDamage(FMath::Sqrt(2*Gravity*Height),Gravity,Depth);};
    TestEqual(TEXT("Normal jump is harmless"),Damage(60),0.f);
    TestTrue(TEXT("Three meters remains safe"),Damage(300)<.001f);
    TestTrue(TEXT("Six meter fall costs 37.5 HP"),FMath::IsNearlyEqual(Damage(600),37.5f,.001f));
    TestTrue(TEXT("Eleven meters is lethal"),FMath::IsNearlyEqual(Damage(1100),100.f,.001f));
    TestTrue(TEXT("Shallow water reduces fall damage"), FMath::IsNearlyEqual(Damage(600,20),27.5f,.001f));
    TestEqual(TEXT("A deep pool absorbs a six metre fall"), Damage(600,210),0.f);
    TestTrue(TEXT("Deep water softens an eleven metre fall"), Damage(1100,210)<.001f);
    TestTrue(TEXT("A very high fall is still lethal in deep water"), FMath::IsNearlyEqual(Damage(2100,210),100.f,.001f));
    TestEqual(TEXT("Upward velocity never damages"),AERCharacter::FallDamage(-2000,Gravity),0.f);
    TestEqual(TEXT("Invalid gravity never damages"),AERCharacter::FallDamage(2000,0),0.f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERWoundOrientationTest,"EclipseRecoil.Player.WoundDecalOrientation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERWoundOrientationTest::RunTest(const FString&)
{
    TestEqual(TEXT("wound1 keeps authored orientation"),UERWounds::RollForVariant(0),0.f);
    TestEqual(TEXT("last attachment wound2 rotates 180 degrees"),UERWounds::RollForVariant(1),180.f);
    TestEqual(TEXT("wound3 keeps authored orientation"),UERWounds::RollForVariant(2),0.f);
    TestEqual(TEXT("wound4 keeps authored orientation"),UERWounds::RollForVariant(3),0.f);
    return true;
}
#endif

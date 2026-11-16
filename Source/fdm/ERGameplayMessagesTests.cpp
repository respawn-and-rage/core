#include "ERGameplayMessages.h"
#include "ERMatch.h"
#include "ERWeapon.h"
#include "ERPlayerController.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERGameplayMessagesTest,"EclipseRecoil.Messages.CatalogAndDelivery",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERGameplayMessagesTest::RunTest(const FString&)
{
    FERMessageLeadState Lead;
    Lead.Update(0,1); TestTrue(TEXT("First sole lead is announced"), Lead.Taken && !Lead.Lost);
    Lead.Update(1,0); TestFalse(TEXT("A tie is not a lost lead"), Lead.Lost);
    Lead.Update(0,-1); TestTrue(TEXT("An overtake after a tie announces both sides"), Lead.Taken && Lead.Lost);
    Lead.Update(-1,0); Lead.Update(0,-1);
    TestTrue(TEXT("Regaining same sole lead is not an opponent overtake"), Lead.Taken && !Lead.Lost);
    UWorld::InitializationValues Values;
    Values.AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false);
    auto* World = UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    if (!TestNotNull(TEXT("Test world"), World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Messages = World->GetSubsystem<UERGameplayMessages>();
    TestEqual(TEXT("All events retained"), Messages->CatalogSize(), 73);
    TestEqual(TEXT("Ordinary notifications allow six seconds to read"), Messages->Events[TEXT("kill")].Duration, 6.f);
    TestEqual(TEXT("Longer result notifications allow ten seconds to read"), Messages->Events[TEXT("team_victory")].Duration, 10.f);
    auto* Player = World->SpawnActor<AERPlayerState>();
    auto* PC = World->SpawnActor<AERPlayerController>();
    PC->SetAsLocalPlayerController(); Player->SetOwner(PC);
    const UERGameplayMessages::FContext Context{{TEXT("actor"),TEXT("Killer")},{TEXT("target"),TEXT("Victim")}};
    TSet<FString> Drawn;
    const int32 Count = Messages->Events[TEXT("kill")].Actor.Num();
    FString Last;
    for (int32 I = 0; I < Count; ++I)
    {
        Last = Messages->Select(Player,TEXT("kill"),false,Context);
        TestFalse(TEXT("No repeated copy within bag"), Drawn.Contains(Last));
        Drawn.Add(Last);
    }
    TestNotEqual(TEXT("No repeat across bag boundary"), Messages->Select(Player,TEXT("kill"),false,Context), Last);
    TSet<FString> DrowningLines;
    for (int32 Variant = 0; Variant < 4; ++Variant)
    {
        const FString Line = Messages->Select(Player, TEXT("drowning_death"), false, Context);
        TestFalse(TEXT("Drowning variant contains text"), Line.IsEmpty());
        DrowningLines.Add(Line);
    }
    TestEqual(TEXT("Drowning has four distinct variants"), DrowningLines.Num(), 4);
    FERMessageEvent Missing;
    Missing.Actor = {TEXT("Unknown {missing}"), TEXT("Safe {actor}")};
    Messages->Events.Add(TEXT("missing"), Missing);
    TestEqual(TEXT("Unavailable placeholders skipped"), Messages->Select(Player,TEXT("missing"),false,Context), FString(TEXT("Safe Killer")));
    TestTrue(TEXT("No unresolved placeholders when all remaining variants lack context"), Messages->Select(Player,TEXT("missing"),false,Context).IsEmpty());
    TestTrue(TEXT("Empty target remains empty"), Messages->Select(Player,TEXT("first_blood"),true,Context).IsEmpty());
    // Presentation is tested directly in this world; real owner RPC delivery is covered by test-network.py.
    PC->ClientGameplayMessage_Implementation(TEXT("kill"),TEXT("KILLED · Victim"),TEXT("Copy"),10,3,true);
    PC->ClientGameplayMessage_Implementation(TEXT("kill"),TEXT("KILLED · Victim2"),TEXT("More copy"),10,3,true);
    TestEqual(TEXT("Every confirmation retained"), PC->KillConfirmations.Num(), 2);
    PC->ClientGameplayMessage_Implementation(TEXT("revenge"),TEXT("Revenge"),TEXT("Revenge copy"),88,3,false);
    PC->ClientGameplayMessage_Implementation(TEXT("headshot"),TEXT("Headshot"),TEXT("Headshot copy"),60,3,false);
    TestEqual(TEXT("Lower priority does not overwrite active recognition"), PC->GameplayToast.Event, FString(TEXT("revenge")));
    TestEqual(TEXT("Bonus preserves confirmations"), PC->KillConfirmations.Num(), 2);
    const FString TargetCopy = Messages->Select(Player,TEXT("revenge"),true,Context);
    TestTrue(TEXT("Victim receives independent target variants"), Messages->Events[TEXT("revenge")].Target.Contains(TargetCopy.Replace(TEXT("Killer"),TEXT("{actor}"))));
    PC->ClientGameplayMessage_Implementation(TEXT("team_victory"),TEXT("Team Victory"),TEXT("Result copy"),100,5,false);
    TestEqual(TEXT("Result preserves final kill confirmations"), PC->KillConfirmations.Num(), 2);
    auto* Life = World->SpawnActor<APawn>();
    PC->Possess(Life);
    TestTrue(TEXT("Source pawn is possessed"), Life->GetController() == PC);
    auto* Projectile = World->SpawnActorDeferred<AERProjectile>(AERProjectile::StaticClass(),FTransform::Identity,nullptr,Life,
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    Projectile->FinishSpawning(FTransform::Identity);
    // This transient unit-test world has not run InitializeActorsForPlay.
    Projectile->PostInitializeComponents();
    TestTrue(TEXT("Projectile captures authoritative attacker"), Projectile->GetDamageController() == PC);
    PC->UnPossess(); Life->Destroy();
    TestTrue(TEXT("Delayed damage retains attacker after source life is destroyed"), Projectile->GetDamageController() == PC);
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}
#endif

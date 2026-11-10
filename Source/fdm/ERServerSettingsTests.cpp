#include "ERServerSettings.h"
#include "ERWeapon.h"
#include "ERWeaponBallistics.h"
#include "ERLoadout.h"
#include "ERCharacter.h"
#include "ERMovement.h"
#include "ERMatch.h"
#include "ERBlockWorld.h"
#include "ERCellWorld.h"
#include "ERMapCatalog.h"
#include "ERLobbyRules.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERServerRulesTest,"EclipseRecoil.ServerSettings.ValidationAndPersistence",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERServerRulesTest::RunTest(const FString&)
{
    FERServerRules R;
    TestEqual(TEXT("Standard smoke lifetime is doubled"),R.Get("Smoke.Duration"),24.f);
    TestEqual(TEXT("Standard running speed"),R.Get("Move.Run"),14.f);
    TestEqual(TEXT("Standard sprint speed"),R.Get("Move.Sprint"),25.f);
    TestEqual(TEXT("Latest sprint duration remains Standard"),R.Get("Move.SprintDuration"),15.f);
    TestEqual(TEXT("Actual weapon value is retained without override"),R.Get("Weapon.Shotgun.Damage",22),22.f);
    R.Set("Move.SprintDuration",7);R.Set("Move.Recovery",11);
    R.Set("Unknown.Arbitrary",10);R.Set("Move.Run",std::numeric_limits<float>::quiet_NaN());
    TestEqual(TEXT("Unknown / nonfinite settings are rejected"),R.Values.Num(),2);
    TestEqual(TEXT("map sizing defaults to three per team"),R.Get("Map.ExpectedTeamSize"),3.f);
    R.Set("Map.ExpectedTeamSize",4.4f);
    TestEqual(TEXT("map sizing is integral"),R.Get("Map.ExpectedTeamSize"),4.f);
    TestEqual(TEXT("map sizing never changes admission capacity"),ERServer::ValidMaxPlayers(33),33);
    R.Set("Health.Max",150);R.Set("Health.Start",200);
    R.Set("Claymore.DetectionAngle",150);R.Set("Claymore.DamageAngle",80);
    R.Set("Weapon.HE.ExplosionRadius",2);R.Set("Weapon.HE.ExplosionInnerRadius",6);
    R.Set("Weapon.Shotgun.Pellets",9.4f);
    R.Set("Move.Step",.5f);R.Set("Move.Vault",.3f);R.Set("Move.Climb",.5f);
    for(const auto& E:ERMaps::All())if(E.InRotation)R.Set(FName(*(FString(TEXT("Map."))+E.Id)),0);
    R.Normalize();
    TestEqual(TEXT("Health cannot exceed maximum"),R.Get("Health.Start"),150.f);
    TestEqual(TEXT("Sensor stays inside damage sector"),R.Get("Claymore.DetectionAngle"),80.f);
    TestEqual(TEXT("Pellet count is integral"),R.Get("Weapon.Shotgun.Pellets"),9.f);
    TestTrue(TEXT("Vault remains above automatic step"),R.Get("Move.Vault")>=R.Get("Move.Step"));
    TestEqual(TEXT("Cannot disable every production map"),ERMaps::Rotation(&R).Num(),1);
    TestEqual(TEXT("Single-map pool keeps that map available to lobby"),ERMaps::LobbyLeaders(TArray<int32>{2,5},&R),TArray<int32>{0});
    TestFalse(TEXT("Depot never enters the server pool"),ERMaps::Enabled(1,&R));
    {
        FERServerRules TownOnly;
        for(const auto& Entry:ERMaps::All())if(Entry.InRotation)
            TownOnly.Set(FName(*(FString(TEXT("Map."))+Entry.Id)),FString(Entry.Id)==TEXT("town") ? 1.f:0.f);
        TownOnly.Normalize();
        TestEqual(TEXT("Town can be the only rotation map"),ERMaps::Rotation(&TownOnly),TArray<FName>{FName(TEXT("/Game/EclipseRecoil/Maps/L_Town"))});
        TestEqual(TEXT("Town-only rules do not enable Central Building"),TownOnly.Get("Map.central_building"),0.f);
    }
    TestEqual(TEXT("Standard still needs strict majority"),ERLobby::RequiredReady(16),9);
    TestEqual(TEXT("Unanimity uses every human"),ERLobby::RequiredReady(16,100),16);
    const FString File=FPaths::ProjectSavedDir()/TEXT("Testing/ServerRules-Roundtrip.ini");
    GConfig->Add(File,FConfigFile());
    ERServer::Save(R,File,TEXT("Test"));
    // Multiprocess deliberately disables global ini flushes. Serialize only this owned fixture.
    FString Serialized;GConfig->Find(File)->WriteToString(Serialized);
    TestTrue(TEXT("Preset serialization is written"),FFileHelper::SaveStringToFile(Serialized,*File));
    GConfig->UnloadFile(File);
    FERServerRules Loaded;ERServer::Load(Loaded,File,TEXT("Test"));
    TestEqual(TEXT("All validated overrides survive save/load"),Loaded.Values.Num(),R.Values.Num());
    for(const auto& V:R.Values)TestEqual(*V.Key.ToString(),Loaded.Get(V.Key),R.Get(V.Key));
    IFileManager::Get().Delete(*File);GConfig->UnloadFile(File);
    TSet<FName> Keys;
    for(const auto& S:ERServer::Settings())
    {
        TestFalse(*(FString(TEXT("Unique key "))+S.Key.ToString()),Keys.Contains(S.Key));Keys.Add(S.Key);
        TestTrue(TEXT("Every field has a valid numeric range"),S.Min<=S.Max && FMath::IsFinite(S.Max));
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERServerWeaponSettingsTest,"EclipseRecoil.ServerSettings.WeaponAndKitOverrides",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERServerWeaponSettingsTest::RunTest(const FString&)
{
    auto* Class=LoadClass<AERCharacter>(nullptr,TEXT("/Game/EclipseRecoil/Core/BP_ERCharacter.BP_ERCharacter_C"));
    if(!TestNotNull(TEXT("Saved catalog"),Class))return false;
    const auto* Catalog=Class->GetDefaultObject<AERCharacter>();
    FERServerRules R;R.Set("Weapons.Damage",1.5f);R.Set("Weapons.Spread",1.f);R.Set("Weapons.Recoil",.75f);
    R.Set("Weapon.Assault.Damage",20);R.Set("Weapon.Assault.MagazineSize",10);
    R.Set("Weapon.Assault.HipAccuracy.Move",12);R.Set("Weapon.HE.ExplosionRadius",2);R.Set("Weapon.HE.ExplosionInnerRadius",6);
    const auto Standard=ERLoadouts::Resolve("rifleman","offense",false,Catalog);
    const auto* Original=Standard.Items[0].Data.Get();const float OriginalDamage=Original->Damage;
    auto* Tuned=DuplicateObject<UERWeaponData>(Original,GetTransientPackage());ERServer::TuneWeapon(Tuned,R,Original->GetFName());
    TestEqual(TEXT("Absolute override followed by global multiplier"),Tuned->Damage,30.f);
    TestEqual(TEXT("Nested precision field is applied"),Tuned->HipAccuracy.Move,12.f);
    TestEqual(TEXT("Scoped recoil multiplier applies to actual worksheet"),Tuned->ScopedRecoil.Magnitude,Original->ScopedRecoil.Magnitude*.75f);
    TestEqual(TEXT("Authored weapon stays immutable"),Original->Damage,OriginalDamage);
    const FERServerRules Defaults;
    for(const auto& D:Catalog->ReferenceArsenal)if(D && D->GetFName()==FName("DA_Pistol"))
    {
        auto* Pistol=DuplicateObject<UERWeaponData>(D.Get(),GetTransientPackage());
        ERServer::TuneWeapon(Pistol,Defaults,D->GetFName());
        TestEqual(TEXT("Standard pistol dispersion halves the global default"),
            Pistol->HipAccuracy.Spread,D->HipAccuracy.Spread*.25f);
        TestEqual(TEXT("Standard pistol recoil halves the global default"),
            Pistol->HipRecoil.Magnitude,D->HipRecoil.Magnitude*.25f);
    }
    for(const auto& D:Catalog->ReferenceArsenal)if(D && D->GetFName()==FName("DA_HE"))
    {
        auto* HE=DuplicateObject<UERWeaponData>(D.Get(),GetTransientPackage());
        ERServer::TuneWeapon(HE,Defaults,D->GetFName());
        TestEqual(TEXT("Standard HE maximum damage radius"),HE->ExplosionRadius,800.f);
        TestEqual(TEXT("Standard HE full damage radius"),HE->ExplosionInnerRadius,400.f);
        ERServer::TuneWeapon(HE,R,D->GetFName());
        TestEqual(TEXT("Meters convert to centimeters"),HE->ExplosionRadius,200.f);
        TestTrue(TEXT("Full damage radius does not exceed maximum"),HE->ExplosionInnerRadius<=HE->ExplosionRadius);
    }
    R.Set("Kit.rifleman.offense.PrimaryLoaded",20);R.Set("Kit.rifleman.offense.PrimaryReserve",66);R.Set("Kit.rifleman.offense.HE",5);
    const auto Kit=ERLoadouts::Resolve("rifleman","offense",false,Catalog,&R);
    TestEqual(TEXT("Configured capacity clamps initial magazine"),Kit.Items[0].Loaded,10);
    TestEqual(TEXT("Overflow moves to reserve without losing rounds"),Kit.Items[0].Reserve,76);
    TestEqual(TEXT("Utility quantity means actual carried items, without hidden doubling"),Kit.Items[3].Loaded,5);
    // Every UI weapon field is a real numeric property; prevents inert controls.
    for(const auto& S:ERServer::Settings())if(!S.Weapon.IsNone())
        for(const auto& D:Catalog->ReferenceArsenal)if(D && D->GetFName()==S.Weapon)
        {
            auto* Copy=DuplicateObject<UERWeaponData>(D.Get(),GetTransientPackage());FERServerRules One;
            One.Set("Weapons.Spread",1.f);One.Set("Weapons.Recoil",1.f);
            One.Set("Weapon.Pistol.SpreadScale",1.f);One.Set("Weapon.Pistol.RecoilScale",1.f);
            const float Value=FMath::Clamp(S.Min+(S.Max-S.Min)*.1f,S.Min,S.Max);
            One.Set(S.Key,Value);ERServer::TuneWeapon(Copy,One,S.Weapon);
            // Radius ordering may clamp the inner radius; all other fields must match.
            if(!S.Property.EndsWith(TEXT("InnerRadius")))
                TestTrue(*S.Key.ToString(),FMath::IsNearlyEqual(ERServer::ReadNumber(Copy,Copy->GetClass(),S.Property),One.Get(S.Key)*S.Factor,.01f));
        }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERServerMaterialsTest,"EclipseRecoil.ServerSettings.MaterialAndPredictionIsolation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERServerMaterialsTest::RunTest(const FString&)
{
    auto* Original=NewObject<UERBlockMaterialCatalog>();auto* Copy=DuplicateObject<UERBlockMaterialCatalog>(Original,GetTransientPackage());
    FERServerRules R;R.Set("Structure.Health",2);R.Set("Material.Wood.Health",1.5f);R.Set("Structure.Explosion",.5f);
    ERServer::TuneMaterials(Copy,R);
    TestEqual(TEXT("Per-material resistance composes with global resistance"),Copy->Find(EERBlockMaterial::Wood)->MaxHealth,Original->Find(EERBlockMaterial::Wood)->MaxHealth*3);
    TestEqual(TEXT("Structural explosion damage changes independently"),Copy->Find(EERBlockMaterial::Wood)->ExplosionDamageScale,Original->Find(EERBlockMaterial::Wood)->ExplosionDamageScale*.5f);
    TestEqual(TEXT("Cell size calibration remains unchanged"),Copy->CellCalibration.CellSize,Original->CellCalibration.CellSize);
    UWorld::InitializationValues V;V.AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false);
    auto* W=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&V);
    if(!TestNotNull(TEXT("Prediction fixture"),W))return false;
    auto* GS=W->SpawnActor<AERGameState>();W->SetGameState(GS);GS->ServerRules=R;GS->ServerRules.Ready=true;
    GS->ServerRules.Set("Move.Run",21);GS->ServerRules.Set("Move.SprintDuration",7);GS->ServerRules.Set("Move.Recovery",11);
    auto* Pawn=W->SpawnActor<AERCharacter>();auto* M=Cast<UERMovement>(Pawn->GetCharacterMovement());M->ApplyServerSettings();
    TestTrue(TEXT("Movement component reads server speed"),FMath::IsNearlyEqual(M->GetMaxSpeed(),21.f/.036f,.01f));
    TestEqual(TEXT("Stamina reads independent durations"),M->SprintDuration(),7.);
    M->Locomotion.RecoverUntil=GS->GetServerWorldTimeSeconds()+5.5;
    TestTrue(TEXT("Stamina HUD remains proportional to configured recovery"),FMath::IsNearlyEqual(M->GetStaminaFraction(),.5f,.01f));
    W->DestroyWorld(false);return true;
}
#endif

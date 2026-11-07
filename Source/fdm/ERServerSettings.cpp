#include "ERServerSettings.h"
#include "ERMatch.h"
#include "ERPlayerController.h"
#include "ERWeapon.h"
#include "ERBlockWorld.h"
#include "ERMapCatalog.h"
#include "Engine/World.h"
#include "Misc/ConfigCacheIni.h"
#include "UObject/UnrealType.h"

namespace
{
FNumericProperty* Number(void*& Container, const UStruct* Type, const FString& Path)
{
    FString Head, Tail;
    if (Path.Split(TEXT("."), &Head, &Tail))
    {
        const auto* Field = FindFProperty<FStructProperty>(Type, *Head);
        if (!Field) return nullptr;
        Container = Field->ContainerPtrToValuePtr<void>(Container);
        return Number(Container, Field->Struct, Tail);
    }
    auto* Field = FindFProperty<FNumericProperty>(Type, *Path);
    if (Field) Container = Field->ContainerPtrToValuePtr<void>(Container);
    return Field;
}
void WriteNumber(void* Container, const UStruct* Type, const FString& Path, float Value)
{
    if (auto* Field = Number(Container, Type, Path))
    {
        if (Field->IsInteger()) Field->SetIntPropertyValue(Container, int64(FMath::RoundToInt(Value)));
        else Field->SetFloatingPointPropertyValue(Container, Value);
    }
}
}

const TArray<FERServerSetting>& ERServer::Settings()
{
    static const TArray<FERServerSetting> Result = []
    {
        TArray<FERServerSetting> R;
        auto Add = [&](const TCHAR* Key, const TCHAR* Section, const TCHAR* Label, const TCHAR* Unit,
            float Default, float Min, float Max, bool Advanced=false, bool Integer=false)
        {
            FERServerSetting S; S.Key=Key; S.Section=Section; S.Label=Label; S.Unit=Unit;
            S.Default=Default; S.Min=Min; S.Max=Max; S.Advanced=Advanced; S.Integer=Integer;
            R.Add(S);
        };
        Add(TEXT("Match.ReadyPercent"),TEXT("Match"),TEXT("READY threshold (strictly above)"),TEXT("%"),50.f,0.f,100.f,true);
        Add(TEXT("Match.Duration"),TEXT("Match"),TEXT("Match duration"),TEXT("s"),600.f,10.f,86400.f,false,false);
        Add(TEXT("Match.ScoreLimit"),TEXT("Match"),TEXT("Score to win"),TEXT("points"),50.f,1.f,100000.f,false,true);
        Add(TEXT("Match.Intermission"),TEXT("Match"),TEXT("Time between matches"),TEXT("s"),10.f,1.f,120.f,true,false);
        Add(TEXT("Match.KillPoints"),TEXT("Match"),TEXT("Points per enemy kill"),TEXT("points"),1.f,0.f,100.f,true,true);
        Add(TEXT("Move.Walk"),TEXT("Movement"),TEXT("Walking speed"),TEXT("km/h"),7.5f,0.1f,50.f,false,false);
        Add(TEXT("Move.Run"),TEXT("Movement"),TEXT("Running speed"),TEXT("km/h"),15.f,0.1f,75.f,false,false);
        Add(TEXT("Move.Sprint"),TEXT("Movement"),TEXT("Sprint speed"),TEXT("km/h"),27.f,0.1f,100.f,false,false);
        Add(TEXT("Move.Crouch"),TEXT("Movement"),TEXT("Crouched speed"),TEXT("km/h"),5.4f,0.1f,30.f,false,false);
        Add(TEXT("Move.Prone"),TEXT("Movement"),TEXT("Crawling speed"),TEXT("km/h"),2.97f,0.1f,20.f,false,false);
        Add(TEXT("Move.Posture"),TEXT("Movement"),TEXT("Speed while changing posture"),TEXT("km/h"),2.43f,0.1f,20.f,true,false);
        Add(TEXT("Move.Acceleration"),TEXT("Movement"),TEXT("Acceleration"),TEXT("cm/s²"),1000.f,10.f,10000.f,true,false);
        Add(TEXT("Move.Braking"),TEXT("Movement"),TEXT("Braking"),TEXT("cm/s²"),850.f,10.f,10000.f,true,false);
        Add(TEXT("Move.Friction"),TEXT("Movement"),TEXT("Ground friction"),TEXT(""),3.f,0.f,20.f,true,false);
        Add(TEXT("Move.AirControl"),TEXT("Movement"),TEXT("Air control"),TEXT(""),0.2f,0.f,1.f,true,false);
        Add(TEXT("Move.SprintDuration"),TEXT("Movement"),TEXT("Full sprint duration"),TEXT("s"),15.f,0.1f,600.f,false,false);
        Add(TEXT("Move.Recovery"),TEXT("Movement"),TEXT("Full stamina recovery"),TEXT("s"),20.f,0.1f,600.f,false,false);
        Add(TEXT("Move.LadderLight"),TEXT("Movement"),TEXT("Ladder speed with light equipment"),TEXT("km/h"),7.776f,0.1f,30.f,true,false);
        Add(TEXT("Move.LadderHeavy"),TEXT("Movement"),TEXT("Ladder speed with stowed weapon"),TEXT("km/h"),9.72f,0.1f,30.f,true,false);
        Add(TEXT("Move.Jump"),TEXT("Traversal"),TEXT("Jump launch speed"),TEXT("m/s"),4.6f,0.1f,15.f,false,false);
        Add(TEXT("Move.Gravity"),TEXT("Traversal"),TEXT("Gravity multiplier"),TEXT("×"),1.8f,0.1f,5.f,true,false);
        Add(TEXT("Move.Step"),TEXT("Traversal"),TEXT("Automatic step height"),TEXT("m"),0.3f,0.05f,0.5f,true,false);
        Add(TEXT("Move.Vault"),TEXT("Traversal"),TEXT("Vault height"),TEXT("m"),0.8f,0.3f,1.5f,true,false);
        Add(TEXT("Move.Climb"),TEXT("Traversal"),TEXT("Climb height"),TEXT("m"),1.5f,0.5f,2.5f,true,false);
        Add(TEXT("Move.TraversalTime"),TEXT("Traversal"),TEXT("Vault / climb duration"),TEXT("×"),1.f,0.25f,4.f,true,false);
        Add(TEXT("Move.CrouchTime"),TEXT("Traversal"),TEXT("Crouch transition"),TEXT("s"),0.4f,0.1f,3.f,true,false);
        Add(TEXT("Move.ProneTime"),TEXT("Traversal"),TEXT("Prone transition"),TEXT("s"),1.1f,0.1f,5.f,true,false);
        Add(TEXT("Health.Max"),TEXT("Health and respawn"),TEXT("Maximum health"),TEXT("HP"),100.f,1.f,10000.f,false,false);
        Add(TEXT("Health.Start"),TEXT("Health and respawn"),TEXT("Starting health"),TEXT("HP"),100.f,1.f,10000.f,false,false);
        Add(TEXT("Health.Armor"),TEXT("Health and respawn"),TEXT("Starting armor"),TEXT("AP"),100.f,0.f,10000.f,false,false);
        Add(TEXT("Health.Helmet"),TEXT("Health and respawn"),TEXT("Starting helmet"),TEXT("on/off"),1.f,0.f,1.f,false,true);
        Add(TEXT("Health.Protection"),TEXT("Health and respawn"),TEXT("Spawn protection"),TEXT("s"),4.f,0.f,60.f,false,false);
        Add(TEXT("Health.Respawn"),TEXT("Health and respawn"),TEXT("Respawn delay"),TEXT("s"),3.f,0.1f,120.f,false,false);
        Add(TEXT("Health.SpawnScoring"),TEXT("Health and respawn"),TEXT("Avoid exposed respawn points"),TEXT("on/off"),1.f,0.f,1.f,true,true);
        Add(TEXT("Damage.Friendly"),TEXT("Health and respawn"),TEXT("Damage to teammates"),TEXT("×"),1.f,0.f,3.f,false,false);
        Add(TEXT("Damage.Self"),TEXT("Health and respawn"),TEXT("Self-inflicted damage"),TEXT("×"),1.f,0.f,3.f,false,false);
        Add(TEXT("Damage.Fall"),TEXT("Health and respawn"),TEXT("Fall damage"),TEXT("×"),1.f,0.f,5.f,false,false);
        Add(TEXT("Damage.FallSafe"),TEXT("Health and respawn"),TEXT("Safe fall height"),TEXT("m"),3.f,0.1f,30.f,true,false);
        Add(TEXT("Damage.FallLethal"),TEXT("Health and respawn"),TEXT("Full-health fall damage height"),TEXT("m"),11.f,0.2f,100.f,true,false);
        Add(TEXT("Damage.Punch"),TEXT("Health and respawn"),TEXT("Aim punch strength"),TEXT("×"),1.f,0.f,4.f,true,false);
        Add(TEXT("Damage.PunchRecovery"),TEXT("Health and respawn"),TEXT("Aim punch half-life"),TEXT("s"),0.2f,0.01f,3.f,true,false);
        Add(TEXT("Damage.Body"),TEXT("Health and respawn"),TEXT("Torso bullet damage"),TEXT("×"),1.f,0.f,4.f,true,false);
        Add(TEXT("Damage.Arms"),TEXT("Health and respawn"),TEXT("Arm bullet damage"),TEXT("×"),1.f,0.f,4.f,true,false);
        Add(TEXT("Damage.Legs"),TEXT("Health and respawn"),TEXT("Leg bullet damage"),TEXT("×"),1.f,0.f,4.f,true,false);
        Add(TEXT("Weapons.Damage"),TEXT("Weapons - global"),TEXT("All firearm damage"),TEXT("×"),1.f,0.f,10.f,false,false);
        Add(TEXT("Weapons.Spread"),TEXT("Weapons - global"),TEXT("All firearm dispersion"),TEXT("×"),1.f,0.f,5.f,false,false);
        Add(TEXT("Weapons.Recoil"),TEXT("Weapons - global"),TEXT("All firearm recoil"),TEXT("×"),1.f,0.f,5.f,false,false);
        Add(TEXT("Weapons.SprintSpread"),TEXT("Weapons - global"),TEXT("Sprint spread penalty"),TEXT("degrees"),1.5f,0.f,10.f,true,false);
        Add(TEXT("Smoke.Growth"),TEXT("Smoke"),TEXT("Expansion duration"),TEXT("s"),2.2f,.1f,30.f,true);
        Add(TEXT("Smoke.Fade"),TEXT("Smoke"),TEXT("Dissipation duration"),TEXT("s"),4.f,.1f,30.f,true);
        Add(TEXT("Smoke.Density"),TEXT("Smoke"),TEXT("Optical density"),TEXT("×"),1.f,.1f,3.f,true);
        Add(TEXT("Smoke.Duration"),TEXT("Smoke"),TEXT("Cloud lifetime"),TEXT("s"),24.f,1.f,120.f,false,false);
        Add(TEXT("Smoke.Radius"),TEXT("Smoke"),TEXT("Maximum horizontal radius"),TEXT("m"),4.4f,1.f,12.f,false,false);
        Add(TEXT("Smoke.Height"),TEXT("Smoke"),TEXT("Cloud height"),TEXT("m"),4.f,1.f,10.f,false,false);
        Add(TEXT("Smoke.ShotRadius"),TEXT("Smoke"),TEXT("Bullet wake radius"),TEXT("m"),0.12f,0.01f,0.9f,true,false);
        Add(TEXT("Smoke.ShotLifetime"),TEXT("Smoke"),TEXT("Bullet wake recovery"),TEXT("s"),2.f,0.1f,10.f,true,false);
        Add(TEXT("Smoke.BlastRadius"),TEXT("Smoke"),TEXT("Blast dispersal radius"),TEXT("×"),1.f,0.1f,3.f,true,false);
        Add(TEXT("Claymore.DamageRadius"),TEXT("Claymore"),TEXT("Damage radius"),TEXT("m"),8.f,0.25f,30.f,false,false);
        Add(TEXT("Claymore.TriggerPercent"),TEXT("Claymore"),TEXT("Trigger radius / damage radius"),TEXT("%"),25.f,1.f,90.f,false,false);
        Add(TEXT("Claymore.DetectionAngle"),TEXT("Claymore"),TEXT("Trigger sector opening"),TEXT("degrees"),60.f,1.f,180.f,false,false);
        Add(TEXT("Claymore.DamageAngle"),TEXT("Claymore"),TEXT("Damage sector opening"),TEXT("degrees"),110.f,1.f,180.f,true,false);
        Add(TEXT("Claymore.Damage"),TEXT("Claymore"),TEXT("Peak player damage"),TEXT("HP"),140.f,0.f,2000.f,false,false);
        Add(TEXT("Claymore.ArmorRatio"),TEXT("Claymore"),TEXT("Armor penetration ratio"),TEXT(""),1.6f,0.f,2.f,true,false);
        Add(TEXT("Claymore.ArmDelay"),TEXT("Claymore"),TEXT("Arming delay"),TEXT("s"),2.f,0.1f,30.f,false,false);
        Add(TEXT("Claymore.Lifetime"),TEXT("Claymore"),TEXT("Lifetime after arming"),TEXT("s"),60.f,1.f,600.f,false,false);
        Add(TEXT("Claymore.Placement"),TEXT("Claymore"),TEXT("Placement duration"),TEXT("s"),2.2f,0.5f,10.f,true,false);
        Add(TEXT("Claymore.StructuralRadius"),TEXT("Claymore"),TEXT("Structural damage radius"),TEXT("m"),1.5f,0.1f,10.f,true,false);
        Add(TEXT("Claymore.StructuralDamage"),TEXT("Claymore"),TEXT("Peak structural damage"),TEXT("HP"),30.f,0.f,5000.f,true,false);
        Add(TEXT("Knife.Head"),TEXT("Knife"),TEXT("Head / neck damage"),TEXT("×"),2.f,0.f,5.f,true,false);
        Add(TEXT("Knife.Limbs"),TEXT("Knife"),TEXT("Limb damage"),TEXT("×"),0.625f,0.f,5.f,true,false);
        Add(TEXT("Knife.RearBonus"),TEXT("Knife"),TEXT("Rear head / torso bonus"),TEXT("HP"),20.f,0.f,1000.f,true,false);
        Add(TEXT("Knife.RearAngle"),TEXT("Knife"),TEXT("Rear sector opening"),TEXT("degrees"),120.f,1.f,180.f,true,false);
        Add(TEXT("Recon.Stock"),TEXT("Recon"),TEXT("Drones per team"),TEXT(""),5.f,0.f,50.f,false,true);
        Add(TEXT("Recon.Replacement"),TEXT("Recon"),TEXT("Drone replacement delay"),TEXT("s"),30.f,1.f,300.f,false,false);
        Add(TEXT("Recon.Range"),TEXT("Recon"),TEXT("Drone sight radius"),TEXT("m"),42.f,5.f,150.f,false,false);
        Add(TEXT("Recon.PlayerAngle"),TEXT("Recon"),TEXT("Player sight cone"),TEXT("degrees"),120.f,10.f,180.f,true,false);
        Add(TEXT("Recon.MinimapRadius"),TEXT("Recon"),TEXT("Minimap display radius"),TEXT("m"),40.f,10.f,150.f,true,false);
        Add(TEXT("Structure.Health"),TEXT("Destruction"),TEXT("All material resistance"),TEXT("×"),1.f,0.1f,10.f,false,false);
        Add(TEXT("Structure.Explosion"),TEXT("Destruction"),TEXT("Explosion damage to structures"),TEXT("×"),1.f,0.f,10.f,false,false);
        Add(TEXT("Map.ExpectedTeamSize"),TEXT("Maps"),TEXT("Expected players per team (map sizing only)"),TEXT("players / team"),2.f,1.f,16.f,false,true);
        Add(TEXT("Map.FixedSeed"),TEXT("Maps"),TEXT("Use a fixed generation seed"),TEXT("on/off"),0.f,0.f,1.f,false,true);
        Add(TEXT("Map.Seed"),TEXT("Maps"),TEXT("Generation seed"),TEXT(""),42.f,0.f,2147483520.f,false,true);
        Add(TEXT("Material.Drywall.Health"),TEXT("Destruction"),TEXT("Drywall resistance"),TEXT("×"),1.f,.1f,10.f,true);
        Add(TEXT("Material.Drywall.Transmission"),TEXT("Destruction"),TEXT("Drywall blast transmission"),TEXT("×"),1.f,0.f,4.f,true);
        Add(TEXT("Material.Brick.Health"),TEXT("Destruction"),TEXT("Brick resistance"),TEXT("×"),1.f,.1f,10.f,true);
        Add(TEXT("Material.Brick.Transmission"),TEXT("Destruction"),TEXT("Brick blast transmission"),TEXT("×"),1.f,0.f,4.f,true);
        Add(TEXT("Material.Concrete.Health"),TEXT("Destruction"),TEXT("Concrete resistance"),TEXT("×"),1.f,.1f,10.f,true);
        Add(TEXT("Material.Concrete.Transmission"),TEXT("Destruction"),TEXT("Concrete blast transmission"),TEXT("×"),1.f,0.f,4.f,true);
        Add(TEXT("Material.Wood.Health"),TEXT("Destruction"),TEXT("Wood resistance"),TEXT("×"),1.f,.1f,10.f,true);
        Add(TEXT("Material.Wood.Transmission"),TEXT("Destruction"),TEXT("Wood blast transmission"),TEXT("×"),1.f,0.f,4.f,true);
        Add(TEXT("Material.Stone.Health"),TEXT("Destruction"),TEXT("Stone resistance"),TEXT("×"),1.f,.1f,10.f,true);
        Add(TEXT("Material.Stone.Transmission"),TEXT("Destruction"),TEXT("Stone blast transmission"),TEXT("×"),1.f,0.f,4.f,true);
        Add(TEXT("Material.Metal.Health"),TEXT("Destruction"),TEXT("Metal resistance"),TEXT("×"),1.f,.1f,10.f,true);
        Add(TEXT("Material.Metal.Transmission"),TEXT("Destruction"),TEXT("Metal blast transmission"),TEXT("×"),1.f,0.f,4.f,true);
        Add(TEXT("Material.Glass.Health"),TEXT("Destruction"),TEXT("Glass resistance"),TEXT("×"),1.f,.1f,10.f,true);
        Add(TEXT("Material.Glass.Transmission"),TEXT("Destruction"),TEXT("Glass blast transmission"),TEXT("×"),1.f,0.f,4.f,true);
        for(const auto& Entry:ERMaps::All())if(Entry.InRotation)
            Add(*(FString(TEXT("Map."))+Entry.Id),TEXT("Maps"),*(FString(Entry.Label)+TEXT(" in rotation")),TEXT("on/off"),1.f,0.f,1.f,false,true);
        // Absolute weapon overrides retain authored values until explicitly edited.
        auto WeaponField = [&](const TCHAR* Id, const TCHAR* Section, const TCHAR* Property, const TCHAR* Label,
            const TCHAR* Unit, float Min, float Max, bool Advanced=false, bool Integer=false, float Factor=1)
        {
            FERServerSetting S; S.Key=FName(*FString::Printf(TEXT("Weapon.%s.%s"),Id,Property));
            S.Section=Section; S.Label=Label; S.Unit=Unit; S.Min=Min; S.Max=Max;
            static const TMap<FString,FString> Labels={
                {TEXT("Spread"),TEXT("Weapon spread")},{TEXT("Stand"),TEXT("Standing dispersion")},{TEXT("Crouch"),TEXT("Crouched dispersion")},
                {TEXT("Move"),TEXT("Movement dispersion")},{TEXT("Ladder"),TEXT("Ladder dispersion")},{TEXT("Jump"),TEXT("Airborne dispersion")},
                {TEXT("JumpInitial"),TEXT("Takeoff dispersion")},{TEXT("JumpApex"),TEXT("Jump apex dispersion")},{TEXT("Land"),TEXT("Landing dispersion")},
                {TEXT("Fire"),TEXT("Dispersion added per shot")},{TEXT("RecoveryStand"),TEXT("Standing recovery")},
                {TEXT("RecoveryStandFinal"),TEXT("Standing sustained-fire recovery")},{TEXT("RecoveryCrouch"),TEXT("Crouched recovery")},
                {TEXT("RecoveryCrouchFinal"),TEXT("Crouched sustained-fire recovery")},{TEXT("Magnitude"),TEXT("Recoil strength")},
                {TEXT("MagnitudeVariance"),TEXT("Recoil strength variation")},{TEXT("AngleVariance"),TEXT("Recoil direction variation")}};
            FString Mode,Field;if(FString(Property).Split(TEXT("."),&Mode,&Field))
                if(const auto* Friendly=Labels.Find(Field))S.Label=(Mode.StartsWith(TEXT("Hip"))?TEXT("Hip fire / "):TEXT("Aimed fire / "))+*Friendly;
            if(S.Unit==TEXT("worksheet units"))S.Unit=TEXT("spread units");
            S.Advanced=Advanced; S.Integer=Integer; S.AssetDefault=true;
            S.Weapon=FName(*(FString(TEXT("DA_"))+Id)); S.Property=Property; S.Factor=Factor; R.Add(S);
        };
        for (const TCHAR* Id : {TEXT("Pistol"),TEXT("SMG"),TEXT("Shotgun"),TEXT("Assault"),TEXT("Carbine"),TEXT("Sniper"),TEXT("Launcher"),TEXT("HE"),TEXT("Smoke"),TEXT("Knife")})
        {
            const FString Name(Id);
            const FString Group=Name==TEXT("HE")?TEXT("HE grenades"):Name==TEXT("Smoke")?TEXT("Smoke"):Name==TEXT("Knife")?TEXT("Knife"):
                Name==TEXT("Launcher")?TEXT("Grenade launcher"):Name==TEXT("Carbine")?TEXT("Weapon - Marksman Rifle"):
                Name==TEXT("Assault")?TEXT("Weapon - Assault Rifle"):Name==TEXT("Sniper")?TEXT("Weapon - Sniper Rifle"):FString(TEXT("Weapon - "))+Id;
            const bool Firearm=FString(Id)!=TEXT("HE") && FString(Id)!=TEXT("Smoke") && FString(Id)!=TEXT("Knife") && FString(Id)!=TEXT("Launcher");
            if(FString(Id)!=TEXT("Smoke"))WeaponField(Id,*Group,TEXT("Damage"),TEXT("Damage per projectile / pellet"),TEXT("HP"),0,2000);
            WeaponField(Id,*Group,TEXT("ShotInterval"),TEXT("Time between shots / attacks"),TEXT("s"),.02f,10);
            if(FString(Id)==TEXT("Knife"))
            {
                WeaponField(Id,*Group,TEXT("HitscanRange"),TEXT("Knife reach"),TEXT("m"),.1f,3,false,false,100);
                continue;
            }
            if(Firearm || FString(Id)==TEXT("Launcher"))
            {
                WeaponField(Id,*Group,TEXT("MagazineSize"),TEXT("Magazine capacity"),TEXT("rounds"),1,500,false,true);
                WeaponField(Id,*Group,TEXT("ReloadDuration"),TEXT("Reload duration"),TEXT("s"),.1f,20);
            }
            if(FString(Id)!=TEXT("Smoke"))WeaponField(Id,*Group,TEXT("ArmorRatio"),TEXT("Armor penetration ratio"),TEXT(""),0,2,true);
            if(Firearm)
            {
                WeaponField(Id,*Group,TEXT("HeadshotMultiplier"),TEXT("Head / neck damage"),TEXT("×"),0,10,true);
                WeaponField(Id,*Group,TEXT("RangeModifier"),TEXT("Damage retained per 12.7 m"),TEXT("×"),.01f,1,true);
                WeaponField(Id,*Group,TEXT("HitscanRange"),TEXT("Maximum trace range"),TEXT("m"),1,1500,true,false,100);
                WeaponField(Id,*Group,TEXT("Pellets"),TEXT("Pellets per shot"),TEXT(""),1,16,true,true);
                if(FString(Id)==TEXT("Assault") || FString(Id)==TEXT("Carbine") || FString(Id)==TEXT("Sniper"))
                    WeaponField(Id,*Group,TEXT("ScopeSettleDuration"),TEXT("Aim settling time"),TEXT("s"),.01f,5,true);
                if(FString(Id)==TEXT("Assault"))WeaponField(Id,*Group,TEXT("ZoomFOV"),TEXT("Aim field of view"),TEXT("degrees"),5,90,true);
                WeaponField(Id,*Group,TEXT("BlockShot.MaxPenetrations"),TEXT("Maximum cover penetrations"),TEXT(""),1,16,true,true);
                for(const TCHAR* Mode : {TEXT("HipAccuracy"),TEXT("ScopedAccuracy")})
                {
                    if(FString(Mode)==TEXT("ScopedAccuracy") && Name!=TEXT("Carbine") && Name!=TEXT("Sniper"))continue;
                    for(const TCHAR* Field : {TEXT("Spread"),TEXT("Stand"),TEXT("Crouch"),TEXT("Move"),TEXT("Ladder"),TEXT("Jump"),TEXT("JumpInitial"),TEXT("JumpApex"),TEXT("Land"),TEXT("Fire")})
                        WeaponField(Id,*Group,*(FString(Mode)+TEXT(".")+Field),*(FString(Mode)+TEXT(" / ")+Field),TEXT("worksheet units"),0,200,true);
                    for(const TCHAR* Field : {TEXT("RecoveryStand"),TEXT("RecoveryStandFinal"),TEXT("RecoveryCrouch"),TEXT("RecoveryCrouchFinal")})
                        WeaponField(Id,*Group,*(FString(Mode)+TEXT(".")+Field),*(FString(Mode)+TEXT(" / ")+Field),TEXT("s"),.01f,10,true);
                }
                for(const TCHAR* Mode : {TEXT("HipRecoil"),TEXT("ScopedRecoil")})
                {
                    if(FString(Mode)==TEXT("ScopedRecoil") && Name!=TEXT("Carbine") && Name!=TEXT("Sniper"))continue;
                    for(const TCHAR* Field : {TEXT("Magnitude"),TEXT("MagnitudeVariance"),TEXT("AngleVariance")})
                        WeaponField(Id,*Group,*(FString(Mode)+TEXT(".")+Field),*(FString(Mode)+TEXT(" / ")+Field),TEXT(""),0,100,true);
                }
            }
            if(FString(Id)==TEXT("HE") || FString(Id)==TEXT("Smoke") || FString(Id)==TEXT("Launcher"))
            {
                WeaponField(Id,*Group,TEXT("FuseDuration"),TEXT("Fuse duration"),TEXT("s"),.1f,20);
                WeaponField(Id,*Group,TEXT("ProjectileSpeed"),TEXT("Launch speed"),TEXT("m/s"),1,100,true,false,100);
                WeaponField(Id,*Group,TEXT("LaunchRangeMultiplier"),TEXT("Horizontal launch multiplier"),TEXT("×"),.1f,4,true);
                if(FString(Id)==TEXT("Launcher"))WeaponField(Id,*Group,TEXT("ScopedLaunchRangeMultiplier"),TEXT("Aimed horizontal launch multiplier"),TEXT("×"),.1f,4,true);
                if(FString(Id)!=TEXT("Smoke"))
                {
                    WeaponField(Id,*Group,TEXT("ExplosionRadius"),TEXT("Maximum player damage radius"),TEXT("m"),.1f,30,false,false,100);
                    WeaponField(Id,*Group,TEXT("ExplosionInnerRadius"),TEXT("Full player damage radius"),TEXT("m"),0,30,false,false,100);
                    WeaponField(Id,*Group,TEXT("BlockExplosion.BaseDamage"),TEXT("Peak structural damage"),TEXT("HP"),0,10000,true);
                    WeaponField(Id,*Group,TEXT("BlockExplosion.InnerRadius"),TEXT("Full structural damage radius"),TEXT("m"),0,20,true,false,100);
                    WeaponField(Id,*Group,TEXT("BlockExplosion.OuterRadius"),TEXT("Maximum structural damage radius"),TEXT("m"),.1f,20,true,false,100);
                    WeaponField(Id,*Group,TEXT("BlockExplosion.FalloffExponent"),TEXT("Structural damage falloff exponent"),TEXT(""),.1f,8,true);
                    WeaponField(Id,*Group,TEXT("BlockExplosion.MaxShieldingCm"),TEXT("Maximum shielding thickness"),TEXT("cm"),0,400,true);
                }
            }
        }
        for(const TCHAR* Preset : {TEXT("breacher"),TEXT("rifleman"),TEXT("marksman"),TEXT("sniper"),TEXT("grenadier")})
            for(const TCHAR* Variant : {TEXT("gunner"),TEXT("offense"),TEXT("defense")})
            {
                const FString Prefix=FString(TEXT("Kit."))+Preset+TEXT(".")+Variant+TEXT(".");
                const FString Group=FString(TEXT("Kit - "))+Preset;
                for(const TCHAR* Field : {TEXT("PrimaryLoaded"),TEXT("PrimaryReserve"),TEXT("SecondaryLoaded"),TEXT("SecondaryReserve"),TEXT("HE"),TEXT("Smoke"),TEXT("Claymore")})
                {
                    const FString FieldName(Field),VariantName(Variant);
                    if((FieldName==TEXT("HE") && VariantName!=TEXT("offense")) ||
                       (FieldName==TEXT("Smoke") && VariantName==TEXT("gunner")) ||
                       (FieldName==TEXT("Claymore") && VariantName!=TEXT("defense")))continue;
                    const FString Key=Prefix+Field,Label=FString(Variant)+TEXT(" / ")+Field;
                    Add(*Key,*Group,*Label,TEXT("rounds / items"),0,0,2000,false,true);
                    R.Last().AssetDefault=true;
                }
            }
        return R;
    }();
    return Result;
}
const FERServerSetting* ERServer::Find(FName Key)
{
    static const TMap<FName,const FERServerSetting*> Index=[] { TMap<FName,const FERServerSetting*> R;for(const auto& S:Settings())R.Add(S.Key,&S);return R; }();
    const auto* Found=Index.Find(Key);return Found?*Found:nullptr;
}
bool FERServerRules::Has(FName Key) const { return Values.ContainsByPredicate([Key](const auto& V){return V.Key==Key;}); }
float FERServerRules::Get(FName Key, float AssetDefault) const
{
    const auto* S=ERServer::Find(Key); if(!S)return AssetDefault;
    if(const auto* V=Values.FindByPredicate([Key](const auto& V){return V.Key==Key;}))
        return FMath::IsFinite(V->Value)?FMath::Clamp(V->Value,S->Min,S->Max):(S->AssetDefault?AssetDefault:S->Default);
    return S->AssetDefault?AssetDefault:S->Default;
}
void FERServerRules::Set(FName Key,float Value)
{
    const auto* S=ERServer::Find(Key); if(!S || !FMath::IsFinite(Value))return;
    Value=FMath::Clamp(Value,S->Min,S->Max);if(S->Integer)Value=FMath::RoundToFloat(Value);
    Reset(Key);
    if(S->AssetDefault || !FMath::IsNearlyEqual(Value,S->Default)) { FERServerValue V;V.Key=Key;V.Value=Value;Values.Add(V); }
}
void FERServerRules::Reset(FName Key) { Values.RemoveAll([Key](const auto& V){return V.Key==Key;}); }
void FERServerRules::Normalize()
{
    const auto Copy=Values; Values.Reset();
    for(const auto& V:Copy)Set(V.Key,V.Value);
    if(Get("Health.Start")>Get("Health.Max"))Set("Health.Start",Get("Health.Max"));
    if(Get("Damage.FallLethal")<=Get("Damage.FallSafe"))Set("Damage.FallLethal",Get("Damage.FallSafe")+.1f);
    if(Get("Move.Vault")<Get("Move.Step"))Set("Move.Vault",Get("Move.Step"));
    if(Get("Move.Climb")<Get("Move.Vault"))Set("Move.Climb",Get("Move.Vault"));
    if(Get("Claymore.DetectionAngle")>Get("Claymore.DamageAngle"))Set("Claymore.DetectionAngle",Get("Claymore.DamageAngle"));
    bool AnyMap=false;
    for(const auto& Entry:ERMaps::All())if(Entry.InRotation)
        AnyMap|=Get(FName(*(FString(TEXT("Map."))+Entry.Id)))>0;
    if(!AnyMap)for(const auto& Entry:ERMaps::All())if(Entry.InRotation)
    {Set(FName(*(FString(TEXT("Map."))+Entry.Id)),1);break;}
}
const FERServerRules& ERServer::Rules(const UWorld* World)
{
    static const FERServerRules Standard;
    if(!World)return Standard;
    if(const auto* GS=World->GetGameState<AERGameState>();GS && GS->ServerRules.Ready)return GS->ServerRules;
    // A remote client's saved host preferences never affect its current server.
    if(World->GetNetMode()!=NM_Client)
        if(const auto* GI=World->GetGameInstance<UERGameInstance>())return GI->SessionRules;
    return Standard;
}
float ERServer::Get(const UWorld* World,FName Key,float AssetDefault) { return Rules(World).Get(Key,AssetDefault); }
void ERServer::Load(FERServerRules& Rules,const FString& File,const FString& Section)
{
    Rules.Values.Reset();
    for(const auto& S:Settings())
    {
        float V=0;if(GConfig->GetFloat(*Section,*S.Key.ToString(),V,File))Rules.Set(S.Key,V);
    }
    Rules.Normalize();
}
void ERServer::Save(const FERServerRules& Rules,const FString& File,const FString& Section)
{
    GConfig->EmptySection(*Section,File);
    for(const auto& V:Rules.Values)GConfig->SetFloat(*Section,*V.Key.ToString(),V.Value,File);
    GConfig->Flush(false,File);
}
FString ERServer::Summary(const FERServerRules& Rules)
{
    if(Rules.IsStandard())return TEXT("Standard gameplay");
    FString Text=FString::Printf(TEXT("Custom gameplay · %d overrides"),Rules.Values.Num());
    for(const auto& V:Rules.Values)
        if(const auto* S=Find(V.Key))Text+=FString::Printf(TEXT("\n%s: %g %s"),*S->Label,V.Value,*S->Unit);
    return Text;
}
float ERServer::ReadNumber(const void* Container,const UStruct* Type,const FString& Property)
{
    void* Value=const_cast<void*>(Container);
    if(auto* P=Number(Value,Type,Property))return P->IsInteger()?float(P->GetSignedIntPropertyValue(Value)):float(P->GetFloatingPointPropertyValue(Value));
    return 0;
}
void ERServer::TuneWeapon(UERWeaponData* Target,const FERServerRules& Rules,FName AssetId)
{
    if(!Target)return;
    for(const auto& S:Settings())
        if(S.Weapon==AssetId && Rules.Has(S.Key))WriteNumber(Target,Target->GetClass(),S.Property,Rules.Get(S.Key)*S.Factor);
    if(Target->Payload==EERPayload::Bullet)
    {
        Target->Damage*=Rules.Get("Weapons.Damage");
        const float Spread=Rules.Get("Weapons.Spread"),Recoil=Rules.Get("Weapons.Recoil");
        for(auto* Mode:{&Target->HipAccuracy,&Target->ScopedAccuracy})
            for(const TCHAR* Field:{TEXT("Spread"),TEXT("Stand"),TEXT("Crouch"),TEXT("Move"),TEXT("Ladder"),TEXT("Jump"),TEXT("JumpInitial"),TEXT("JumpApex"),TEXT("Land"),TEXT("Fire")})
                WriteNumber(Mode,FERWeaponAccuracyMode::StaticStruct(),Field,ReadNumber(Mode,FERWeaponAccuracyMode::StaticStruct(),Field)*Spread);
        for(auto* Mode:{&Target->HipRecoil,&Target->ScopedRecoil}) { Mode->Magnitude*=Recoil;Mode->MagnitudeVariance*=Recoil; }
        Target->StandingSpreadDegrees*=Spread;Target->RunningSpreadDegrees*=Spread;
        Target->AirborneSpreadDegrees*=Spread;Target->LadderMovingSpreadDegrees*=Spread;
        Target->BloomPerShotDegrees*=Spread;Target->MaxBloomDegrees*=Spread;
        Target->PelletSpreadDegrees*=Spread;Target->NoScopeSpreadDegrees*=Spread;
        Target->ScopedRunningSpreadDegrees*=Spread;Target->ScopedLowMovingSpreadDegrees*=Spread;
    }
    Target->ExplosionInnerRadius=FMath::Clamp(Target->ExplosionInnerRadius,0.f,Target->ExplosionRadius);
    Target->BlockExplosion.InnerRadius=FMath::Clamp(Target->BlockExplosion.InnerRadius,0.f,Target->BlockExplosion.OuterRadius-.01f);
}
void ERServer::TuneMaterials(UERBlockMaterialCatalog* Target,const FERServerRules& Rules)
{
    if(!Target)return;
    const TCHAR* Names[]={TEXT("Drywall"),TEXT("Brick"),TEXT("Concrete"),TEXT("Wood"),TEXT("Stone"),TEXT("Metal"),TEXT("Glass")};
    for(auto& M:Target->Materials)
    {
        const FString Prefix=FString(TEXT("Material."))+Names[uint8(M.Type)];
        M.MaxHealth*=Rules.Get("Structure.Health")*Rules.Get(FName(*(Prefix+TEXT(".Health"))));
        M.ExplosionDamageScale*=Rules.Get("Structure.Explosion");
        M.ExplosionTransmission=FMath::Clamp(M.ExplosionTransmission*Rules.Get(FName(*(Prefix+TEXT(".Transmission")))),0.f,1.f);
    }
}

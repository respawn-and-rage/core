#include "ERMatchStats.h"
#include "ERCharacter.h"
#include "ERMatch.h"
#include "ERProceduralMap.h"
#include "ERWeaponBallistics.h"
#include "Async/Async.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonWriter.h"
#include "Tasks/Pipe.h"

namespace
{
DEFINE_LOG_CATEGORY_STATIC(LogERMatchStats, Log, All);

UE::Tasks::FPipe& Archive()
{
    static UE::Tasks::FPipe Pipe(TEXT("MatchStatsArchive"));
    return Pipe;
}

const TCHAR* TeamName(uint8 Team)
{
    switch (static_cast<EERTeam>(Team))
    {
    case EERTeam::Alpha: return TEXT("alpha");
    case EERTeam::Omega: return TEXT("omega");
    default: return TEXT("none");
    }
}

const TCHAR* BodyZone(FName Bone)
{
    using namespace ERWeaponBallistics;
    switch (ZoneForBone(Bone))
    {
    case EHitZone::Head: return TEXT("head");
    case EHitZone::Stomach: return TEXT("stomach");
    case EHitZone::Legs: return TEXT("legs");
    default: return MeleeMultiplier(Bone) < 1.f ? TEXT("arms") : TEXT("chest");
    }
}

FString MatchStatsPython(const FString& Script)
{
    FString Override = FPlatformMisc::GetEnvironmentVariable(TEXT("RR_MATCH_STATS_PYTHON"));
    if (!Override.IsEmpty()) return Override;

#if PLATFORM_MAC && PLATFORM_CPU_ARM_FAMILY
    const TCHAR* Runtime = TEXT("mac-arm64/bin/python3.12");
#elif PLATFORM_MAC
    const TCHAR* Runtime = TEXT("mac-x64/bin/python3.12");
#elif PLATFORM_WINDOWS
    const TCHAR* Runtime = TEXT("windows-x64/python.exe");
#elif PLATFORM_LINUX
    const TCHAR* Runtime = TEXT("linux-x64/bin/python3.12");
#else
    return {};
#endif
#if PLATFORM_MAC || PLATFORM_WINDOWS || PLATFORM_LINUX
    FString Bundled = FPaths::GetPath(Script) / TEXT("MatchStatsPython") / Runtime;
    return FPaths::FileExists(Bundled) ? Bundled : FString();
#endif
}

FString MatchStatsDirectory()
{
    FString Directory;
    FParse::Value(FCommandLine::Get(), TEXT("ERMatchStatsDir="), Directory);
    if (Directory.IsEmpty()) Directory = FPlatformMisc::GetEnvironmentVariable(TEXT("RR_MATCH_STATS_DIR"));
    if (Directory.IsEmpty()) Directory = FPaths::ProjectSavedDir() / TEXT("MatchStats");
    return FPaths::ConvertRelativePathToFull(Directory);
}

FString MatchStatsScript()
{
    return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("scripts/export-match-stats.py"));
}
}

void FERMatchStats::Begin(const AERGameMode& Mode)
{
    *this = {};
    if (FParse::Param(FCommandLine::Get(), TEXT("ERNoMatchStats"))) return;
    MatchId = FGuid::NewGuid();
    Started = FDateTime::UtcNow();
    Map = UGameplayStatics::GetCurrentLevelName(&Mode, true);
    const auto* Scores = Mode.GetGameState<AERGameState>();
    ModeName = Scores && Scores->MatchMode == EERMatchMode::SkullHunt
        ? TEXT("skull_hunt") : TEXT("team_deathmatch");
    for (TActorIterator<AERProceduralMap> It(Mode.GetWorld()); It; ++It)
    {
        if (!It->IsMapReady()) continue;
        Seed = It->LayoutConfig.Seed;
        break;
    }
}

int32 FERMatchStats::Participant(const AERPlayerState* State)
{
    if (!State) return INDEX_NONE;
    const FObjectKey Key(State);
    const uint8 Team = static_cast<uint8>(State->Team);
    const FString Name = State->GetPlayerName();
    const int32* Latest = LatestParticipant.Find(Key);
    if (Latest && Participants[*Latest].Team == Team && Participants[*Latest].Name == Name)
        return *Latest;
    const int32 Id = Latest ? Participants[*Latest].PlayerId : LatestParticipant.Num();
    const int32 Index = Participants.Add({Id, Name, Team, State->IsABot()});
    LatestParticipant.Add(Key, Index);
    return Index;
}

int32 FERMatchStats::RememberAttacker(const AERPlayerState* State)
{
    return MatchId.IsValid() ? Participant(State) : INDEX_NONE;
}

void FERMatchStats::Record(const AERCharacter& Victim, const AController* Attacker,
    const FERMessageDamage& Facts, const FHitResult* Hit, float HealthLost, float ArmorLost)
{
    if (!MatchId.IsValid()) return;
    ECause Cause = ECause::Other;
    if (Facts.Fall) Cause = ECause::Fall;
    else if (Facts.Drowning) Cause = ECause::Drowning;
    else if (Facts.OutOfWorld) Cause = ECause::OutOfWorld;
    else if (Facts.Explosion) Cause = ECause::Explosion;
    else if (Facts.Bullet) Cause = ECause::Bullet;
    else if (Facts.Melee) Cause = ECause::Melee;
    else if (Facts.Projectile) Cause = ECause::Projectile;

    const bool bImpact = Hit && Hit->bBlockingHit;
    const FName Bone = Hit && (Facts.Bullet || Facts.Melee) ? Hit->BoneName : NAME_None;
    const auto* AttackerState = Attacker ? Attacker->GetPlayerState<AERPlayerState>() : nullptr;
    const int32 AttackerIndex = AttackerState ? Participant(AttackerState) : Facts.StatsAttacker;
    Events.Add({
        FDateTime::UtcNow(),
        AttackerIndex,
        Participant(Victim.GetPlayerState<AERPlayerState>()),
        Facts.DamageWeapon, Bone, bImpact ? FVector3f(Hit->ImpactPoint) : FVector3f::ZeroVector,
        HealthLost, ArmorLost, Cause, bImpact, Victim.Health->Health <= 0.f, Facts.Headshot
    });
}

void FERMatchStats::Finish(const AERGameState& Scores)
{
    if (!MatchId.IsValid()) return;
    Finished = FDateTime::UtcNow();
    Day = FDateTime::Now().ToString(TEXT("%Y-%m-%d"));
    AlphaScore = Scores.AlphaScore;
    OmegaScore = Scores.OmegaScore;
    Winner = static_cast<uint8>(Scores.Winner);

    const FString Directory = MatchStatsDirectory();
    const FString Script = MatchStatsScript();
    const FString Python = MatchStatsPython(Script);

    LatestParticipant.Reset();
    FERMatchStats Completed = MoveTemp(*this);
    *this = {};
    Archive().Launch(TEXT("SaveMatchStats"), [Match = MoveTemp(Completed), Directory, Python, Script]
    {
        if (!Match.Events.IsEmpty()) Match.Write(Directory);
        if (Python.IsEmpty() || !IFileManager::Get().DirectoryExists(*(Directory / TEXT(".pending")))) return;

        FString Error;
        int32 ReturnCode = -1;
        const FString Args = FString::Printf(TEXT("-I -B \"%s\" \"%s\""), *Script, *Directory);
        if (!FPlatformProcess::ExecProcess(*Python, *Args, &ReturnCode, nullptr, &Error) || ReturnCode != 0)
            UE_LOG(LogERMatchStats, Error, TEXT("Daily Parquet export failed (%d). Pending data in %s. %s"),
                ReturnCode, *Directory, *Error);
    }, UE::Tasks::ETaskPriority::BackgroundNormal);
}

void FERMatchStats::ReadGraph(const FString& Day, const FString& Match, TFunction<void(FString)> Complete)
{
    const FString Directory = MatchStatsDirectory();
    const FString Script = MatchStatsScript();
    const FString Python = MatchStatsPython(Script);
    Archive().Launch(TEXT("ReadMatchGraph"), [Directory, Script, Python, Day, Match, Complete = MoveTemp(Complete)]() mutable
    {
        FString Result;
        if (Python.IsEmpty())
        {
            Result = TEXT("{\"error\":\"The host's match archive is unavailable.\"}");
        }
        else
        {
            FString Args = FString::Printf(TEXT("-I -B \"%s\" \"%s\" --graph"), *Script, *Directory);
            if (!Day.IsEmpty()) Args += TEXT(" --day=") + Day;
            if (!Match.IsEmpty()) Args += TEXT(" --match=") + Match;
            FString Error;
            int32 ExitCode = -1;
            if (!FPlatformProcess::ExecProcess(*Python, *Args, &ExitCode, &Result, &Error) || ExitCode != 0)
            {
                UE_LOG(LogERMatchStats, Warning, TEXT("Could not read match graph (%d): %s"), ExitCode, *Error);
                Result = TEXT("{\"error\":\"The host could not read this match archive.\"}");
            }
        }
        AsyncTask(ENamedThreads::GameThread, [Complete = MoveTemp(Complete), Result = MoveTemp(Result)]() mutable
        {
            Complete(MoveTemp(Result));
        });
    }, UE::Tasks::ETaskPriority::BackgroundNormal);
}

void FERMatchStats::WaitForWrites()
{
    Archive().WaitUntilEmpty();
}

void FERMatchStats::Write(const FString& Directory) const
{
    const FString Pending = Directory / TEXT(".pending");
    IFileManager::Get().MakeDirectory(*Pending, true);
    const FString Id = MatchId.ToString(EGuidFormats::DigitsWithHyphensLower);
    const FString Path = Pending / (Id + TEXT(".json"));
    const FString Temp = Path + TEXT(".tmp");
    FString Json;
    const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("schema_version"), 1);
    Writer->WriteValue(TEXT("match_id"), Id);
    Writer->WriteValue(TEXT("match_date"), Day);
    Writer->WriteValue(TEXT("match_started_at"), Started.ToIso8601());
    Writer->WriteValue(TEXT("match_ended_at"), Finished.ToIso8601());
    Writer->WriteValue(TEXT("map"), Map);
    if (Seed.IsSet()) Writer->WriteValue(TEXT("map_seed"), Seed.GetValue());
    Writer->WriteValue(TEXT("mode"), ModeName);
    Writer->WriteValue(TEXT("alpha_score"), AlphaScore);
    Writer->WriteValue(TEXT("omega_score"), OmegaScore);
    Writer->WriteValue(TEXT("winner"), TeamName(Winner));
    Writer->WriteArrayStart(TEXT("events"));
    const TCHAR* Causes[] = {TEXT("other"), TEXT("bullet"), TEXT("melee"), TEXT("projectile"),
        TEXT("explosion"), TEXT("fall"), TEXT("drowning"), TEXT("out_of_world")};
    for (int32 Index = 0; Index < Events.Num(); ++Index)
    {
        const FEvent& Event = Events[Index];
        Writer->WriteObjectStart();
        auto WritePlayer = [&](const FString& Prefix, int32 Index)
        {
            if (!Participants.IsValidIndex(Index)) return;
            const FParticipant& Player = Participants[Index];
            Writer->WriteValue(Prefix + TEXT("_id"), Player.PlayerId);
            Writer->WriteValue(Prefix + TEXT("_name"), Player.Name);
            Writer->WriteValue(Prefix + TEXT("_team"), TeamName(Player.Team));
            Writer->WriteValue(Prefix + TEXT("_is_bot"), Player.bBot);
        };
        WritePlayer(TEXT("attacker"), Event.Attacker);
        WritePlayer(TEXT("victim"), Event.Victim);
        Writer->WriteValue(TEXT("event_at"), Event.Time.ToIso8601());
        Writer->WriteValue(TEXT("event_index"), Index);
        Writer->WriteValue(TEXT("cause"), Causes[static_cast<uint8>(Event.Cause)]);
        if (!Event.Weapon.IsNone()) Writer->WriteValue(TEXT("weapon"), Event.Weapon.ToString());
        if (!Event.Bone.IsNone())
        {
            Writer->WriteValue(TEXT("bone"), Event.Bone.ToString());
            Writer->WriteValue(TEXT("body_zone"), BodyZone(Event.Bone));
        }
        if (Event.bImpact)
        {
            Writer->WriteValue(TEXT("impact_x_cm"), Event.Impact.X);
            Writer->WriteValue(TEXT("impact_y_cm"), Event.Impact.Y);
            Writer->WriteValue(TEXT("impact_z_cm"), Event.Impact.Z);
        }
        Writer->WriteValue(TEXT("hp_damage"), Event.HealthLost);
        Writer->WriteValue(TEXT("armor_damage"), Event.ArmorLost);
        Writer->WriteValue(TEXT("is_death"), Event.bDeath);
        Writer->WriteValue(TEXT("is_headshot"), Event.bHeadshot);
        Writer->WriteObjectEnd();
    }
    Writer->WriteArrayEnd();
    Writer->WriteObjectEnd();
    Writer->Close();
    if (FFileHelper::SaveStringToFile(Json, *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) &&
        IFileManager::Get().Move(*Path, *Temp, false, false, false, true))
    {
        UE_LOG(LogERMatchStats, Display, TEXT("Archived %d events for match %s"), Events.Num(), *Id);
        return;
    }
    UE_LOG(LogERMatchStats, Error, TEXT("Could not save match %s to %s"), *Id, *Path);
}

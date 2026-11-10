#include "ERPlayerController.h"
#include "ERMatch.h"
#include "EROnline.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Common/UdpSocketBuilder.h"
#include "IPAddress.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Kismet/GameplayStatics.h"

namespace
{
int32 DiscoveryPort()
{
    int32 Port = 7778;
    FParse::Value(FCommandLine::Get(), TEXT("ERDiscoveryPort="), Port);
    return FMath::Clamp(Port, 1, 65535);
}
}
void UERGameInstance::CloseDiscoverySocket()
{
    if (!DiscoverySocket) return;
    DiscoverySocket->Close();
    ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(DiscoverySocket);
    DiscoverySocket = nullptr;
}
void UERGameInstance::CancelLANSearch()
{
    SearchDeadline = 0;
    CloseDiscoverySocket();
}
int32 UERGameInstance::LANSecondsLeft() const
{
    return FMath::Max(0, FMath::CeilToInt(SearchDeadline - FPlatformTime::Seconds()));
}
void UERGameInstance::FindLANServer()
{
    CancelLANSearch();
    LastError.Empty();
    DiscoverySocket = FUdpSocketBuilder(TEXT("RR LAN search")).AsNonBlocking().WithBroadcast().BoundToPort(0);
    if (!DiscoverySocket) { LastError = TEXT("LAN search unavailable. Check local network permissions, then retry."); return; }
    DiscoveryNonce = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    SearchDeadline = FPlatformTime::Seconds() + 30;
    NextDiscoveryAt = 0;
    UE_LOG(LogTemp, Display, TEXT("ER_LAN_SEARCH started timeout=30 port=%d"), DiscoveryPort());
}
void UERGameInstance::StartLANServer()
{
    CancelLANSearch();
    SaveProfile();
    CommitServerRules();
    LastError.Empty();
    SessionMode = 1;
    // Packaged games do not use play.command. Explicitly select IPv4 any-address
    // for hosting unless a developer supplied a deliberate bind override.
    FString BindOverride;
    if (!FParse::Value(FCommandLine::Get(), TEXT("MULTIHOME="), BindOverride))
        FCommandLine::Append(TEXT(" -MULTIHOME=0.0.0.0"));
    const FString Options = FString::Printf(TEXT("listen?ERLobby=1?Name=%s?Team=%d?Primary=%d?AlphaSkin=%d?OmegaSkin=%d?ERAlphaBots=%d?EROmegaBots=%d?Magazines=%d?HE=%d?Smoke=%d?Stun=%d?ERMaxPlayers=%d?MaxPlayers=%d"),
        *AERGameMode::SanitizeName(LocalName), PlayerTeam, GetStartingWeapon(), AlphaSkin, OmegaSkin,
        FMath::Clamp(AlphaBots, 0, 10), FMath::Clamp(OmegaBots, 0, 10), SpareMagazines, HECount, SmokeCount, StunCount, MaxPlayers, MaxPlayers);
    UGameplayStatics::OpenLevel(GetWorld(), TEXT("/Game/EclipseRecoil/Maps/L_MainMenu"), true, Options);
}
bool UERGameInstance::TickDiscovery(float DeltaTime)
{
    if (bQuitRequested) { CloseDiscoverySocket(); return true; }
    UWorld* World = GetWorld();
    if (!World) return true;
    // An EOS dedicated server admits only EOS players: never advertise it on LAN.
    const bool Hosting = !GetSubsystem<UEROnline>()->IsOnline() && !UEROnline::RequiresOnline() && (World->GetNetMode() == NM_ListenServer || World->GetNetMode() == NM_DedicatedServer);
    const double Now = FPlatformTime::Seconds();
    if (Hosting && !DiscoverySocket)
    {
        if (Now < NextDiscoveryAt) return true;
        NextDiscoveryAt = Now + 2;
        DiscoverySocket = FUdpSocketBuilder(TEXT("RR LAN beacon")).AsNonBlocking().WithBroadcast().BoundToPort(DiscoveryPort());
        if (!DiscoverySocket) LastError = TEXT("LAN discovery could not bind UDP 7778. Close another local server or check network permissions.");
        if (DiscoverySocket) UE_LOG(LogTemp, Display, TEXT("ER_LAN_BEACON bound=0.0.0.0:%d game_port=%d"), DiscoveryPort(), World->URL.Port);
    }
    if (!Hosting && SearchDeadline > 0 && Now >= SearchDeadline)
    {
        CancelLANSearch();
        LastError = TEXT("No available server found in 30 seconds. Check that both players are on the same LAN and the host has started a server.");
        UE_LOG(LogTemp, Display, TEXT("ER_LAN_SEARCH timeout"));
    }
    if (!Hosting && SearchDeadline == 0) { CloseDiscoverySocket(); return true; }
    if (!DiscoverySocket) return true;
    auto Send = [this](const FString& Message, const FInternetAddr& Address)
    {
        FTCHARToUTF8 Bytes(*Message);
        int32 Sent = 0;
        DiscoverySocket->SendTo(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length(), Sent, Address);
    };
    if (!Hosting && Now >= NextDiscoveryAt)
    {
        NextDiscoveryAt = Now + 1;
        auto Address = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
        Address->SetBroadcastAddress();
        Address->SetPort(DiscoveryPort());
        Send(TEXT("RRLAN1?") + DiscoveryNonce, *Address);
        // Also discover a server on this computer (useful for local testing).
        bool Valid = false;
        Address->SetIp(TEXT("127.0.0.1"), Valid);
        Address->SetPort(DiscoveryPort());
        Send(TEXT("RRLAN1?") + DiscoveryNonce, *Address);
    }
    uint32 Pending = 0;
    int32 Budget = 32;
    while (Budget-- > 0 && DiscoverySocket->HasPendingData(Pending))
    {
        uint8 Buffer[1025]; int32 Read = 0;
        auto Sender = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
        if (!DiscoverySocket->RecvFrom(Buffer, 1024, Read, *Sender) || Read <= 0) break;
        Buffer[Read] = 0;
        const FString Message = UTF8_TO_TCHAR(reinterpret_cast<const char*>(Buffer));
        if (Hosting)
        {
            auto* Mode = World->GetAuthGameMode<AERGameMode>();
            const auto* State = World->GetGameState<AERGameState>();
            if (!Mode || !Mode->HasOpenPlayerSlot() || (State && State->bLobbyStarting)) continue;
            if (Message.StartsWith(TEXT("RRLAN1?")) && Message.Len() == 39)
                Send(FString::Printf(TEXT("RRLAN1!|%s|%d|%s"), *Message.Mid(7), World->URL.Port, *BuildVersion), *Sender);
        }
        else
        {
            TArray<FString> Parts;
            Message.ParseIntoArray(Parts, TEXT("|"), false);
            if (Parts.Num() != 4 || Parts[0] != TEXT("RRLAN1!") || Parts[1] != DiscoveryNonce) continue;
            if (Parts[3] != BuildVersion)
            { LastError = TEXT("A server with a different game version was found. Looking for a compatible server..."); continue; }
            const FString Address = Sender->ToString(false) + TEXT(":") + Parts[2];
            if (!IsValidAddress(Address)) continue;
            UE_LOG(LogTemp, Display, TEXT("ER_LAN_FOUND address=%s"), *Address);
            Join(Address);
            return true; // Join closes the socket.
        }
    }
    return true;
}

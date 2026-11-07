#include "ERCellRecovery.h"
#include "Net/UnrealNetwork.h"

AERCellRecoveryManifest::AERCellRecoveryManifest()
{
    bReplicates=true;bOnlyRelevantToOwner=true;bAlwaysRelevant=false;NetPriority=.5f;
    SetNetUpdateFrequency(5);PrimaryActorTick.bCanEverTick=false;
}
void AERCellRecoveryManifest::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AERCellRecoveryManifest,Room);DOREPLIFETIME(AERCellRecoveryManifest,RecoveryId);
    DOREPLIFETIME(AERCellRecoveryManifest,Descriptor);DOREPLIFETIME(AERCellRecoveryManifest,Pages);
}
void AERCellRecoveryManifest::OnRep_State()
{
    if(Room)Room->ReceiveCellRecovery(this);
}
AERCellRecoveryShard::AERCellRecoveryShard()
{
    bOnlyRelevantToOwner=true;bAlwaysRelevant=false;NetPriority=.5f;SetNetUpdateFrequency(5);
}
void AERCellRecoveryShard::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(AERCellRecoveryShard,RecoveryId);
}
void AERCellRecoveryShard::OnRep_Recovery()
{
    OnRep_State();
}

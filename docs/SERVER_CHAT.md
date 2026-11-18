# Dedicated server in Google Chat

**Code:** `scripts/server-chat/app.py`, `scripts/server-chat/stack.yaml`,
`scripts/server-chat/render-template.py`.

Send `start`, `status`, or `stop` in a direct message to **RespawnAndRage**.
In a space with the app installed, prefix the command with `@RespawnAndRage`.
Once ECS reports the task running, `status` gives the current public
`IP:7777`. The address can change on each launch. After `stop`, use `status`
to confirm shutdown.

The bot reuses the Fargate task definition and public network settings.
Keep the old service at zero replicas. DynamoDB tracks one standalone task
and ECS deduplicates retries. The task stays stopped until a new launch.

## Deploy and configure

Run `python3 scripts/server-chat/render-template.py` to render a CloudFormation
template with the Lambda source embedded. Deploy it with the existing cluster,
service name, task definition ARN, execution role ARN, public subnet IDs, and
security group IDs. Keep deployment values in private configuration. If a task
role is added to the task definition, scope `iam:PassRole` to that role as well.

Configure an interactive Google Chat app with the `ChatEndpoint` stack output
as its HTTP endpoint and **HTTP endpoint URL** as its authentication audience.
Use private Marketplace distribution for organization-wide discovery. Send
`whoami` in a direct message to obtain the Workspace domain ID for
`AllowedDomainId` or the individual user ID for `AllowedUsers`.

If the bot asks for ECS review of an unconfirmed launch, check for tasks
started by `rr-google-chat`. Clear the `game-server` item from the table in
the `StateTable` stack output only after confirming there is no active task
or launch in flight.

## Verify

With boto3 installed, run `python3 -m unittest discover -s scripts/server-chat`.
In Chat, start a test task, check the address with `status`, then send `stop`
and confirm ECS leaves the task stopped.

## Statistics volume

The game task uses an S3 Files access point backed by
`rr-stats-010112784456-eu-south-1-an` in `eu-south-1`. Its `match-stats`
directory is mounted read-write at `/data/match-stats`, with
`RR_MATCH_STATS_DIR=/data/match-stats`. The access point enforces uid/gid 10001.
NFS access is restricted to the game task security group. Preserve the task role,
volume, mount and environment setting when registering new task revisions, and
update the Chat stack's TaskDefinitionArn to that revision.

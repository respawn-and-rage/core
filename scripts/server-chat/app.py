"""Synchronous Google Chat endpoint for the standalone Fargate game server."""
import base64
import hashlib
import json
import logging
import os
import re
import time
from datetime import datetime

import boto3
from boto3.dynamodb.conditions import Attr
from botocore.config import Config
from botocore.exceptions import BotoCoreError, ClientError

LOG = logging.getLogger(__name__)
CONFIG = Config(connect_timeout=2, read_timeout=3, retries={"total_max_attempts": 1})
KEY = {"id": "game-server"}
STOPPED = "Il server è spento. Usa start per avviarlo."
STOPPING = "Il server si sta spegnendo. Usa status tra poco."
WAIT_TO_STOP = "Avvio in corso. Riprova con stop tra poco."
STARTING = "Avvio richiesto. Usa status tra poco per ottenere l’indirizzo."
CHECK = "Non riesco a confermare lo stato del server. Riprova con status tra poco."
REVIEW = "Avvio da verificare nella console ECS prima di riprovare."
USER_ID = re.compile(r"users/[0-9]+")
MESSAGE_ID = re.compile(r"spaces/[^/]+/messages/[^/]+")
CHAT_SERVICE = "chat@system.gserviceaccount.com"
STARTED_BY = "rr-google-chat"


def response(text=None, code=200):
    return {"statusCode": code, "headers": {"content-type": "application/json"},
            "body": json.dumps({"text": text} if text else {})}


def chat_request(event, allowed_users, allowed_domain=""):
    """Trust only claims from API Gateway's JWT authorizer and Google Chat's event."""
    claims = event.get("requestContext", {}).get("authorizer", {}).get("jwt", {}).get("claims", {})
    if (claims.get("email") != CHAT_SERVICE
            or str(claims.get("email_verified")).lower() != "true"):
        return response(code=401)
    try:
        raw = event.get("body", "")
        chat = json.loads(base64.b64decode(raw, validate=True) if event.get("isBase64Encoded") else raw)
        if not isinstance(chat, dict):
            return response(code=400)
    except (ValueError, TypeError):
        return response(code=400)

    if chat.get("type") != "MESSAGE":
        return response()
    user, message = chat.get("user") or {}, chat.get("message") or {}
    if not isinstance(user, dict) or not isinstance(message, dict):
        return response(code=400)
    name = user.get("name", "")
    if user.get("type") != "HUMAN" or not isinstance(name, str) or not USER_ID.fullmatch(name):
        return response()
    raw_command = message.get("argumentText", message.get("text", ""))
    command = raw_command.strip().lower() if isinstance(raw_command, str) else ""
    if command == "whoami":
        space = chat.get("space")
        if isinstance(space, dict) and (space.get("spaceType") == "DIRECT_MESSAGE"
                                        or space.get("type") == "DM"):
            domain = user.get("domainId")
            suffix = f" ID dominio Workspace: {domain}." if domain else ""
            return response(f"Il tuo ID Google Chat è {name}.{suffix}")
        return response("Usa whoami in un messaggio diretto con l’app.")
    if name not in allowed_users and not (allowed_domain and user.get("domainId") == allowed_domain):
        return response("Questo account non è autorizzato a usare i comandi del server.")
    if command == "status":
        return command, None
    if command not in {"start", "stop"}:
        return response("Usa start per avviare il server, stop per spegnerlo e status per vedere stato e indirizzo.")
    identity = message.get("name", "")
    try:
        created = datetime.fromisoformat(message["createTime"])
        if created.tzinfo is None:
            return response(code=400)
        age = time.time() - created.timestamp()
    except (KeyError, TypeError, ValueError, OverflowError):
        return response(code=400)
    if not isinstance(identity, str) or not MESSAGE_ID.fullmatch(identity) or not -60 <= age <= 600:
        if command == "stop":
            return response("Messaggio di arresto scaduto o non valido. Invia un nuovo stop.")
        return response("Messaggio di avvio scaduto o non valido. Invia un nuovo start.")
    if command == "stop":
        return command, None, created
    return command, hashlib.sha256(identity.encode()).hexdigest()


class GameServer:
    def __init__(self, ecs, ec2, table, settings):
        self.ecs, self.ec2, self.table = ecs, ec2, table
        self.cluster = settings["CLUSTER"]
        self.service = settings["LEGACY_SERVICE"]
        self.definition = settings["TASK_DEFINITION"]
        self.network = {"awsvpcConfiguration": {
            "subnets": settings["SUBNETS"].split(","),
            "securityGroups": settings["SECURITY_GROUPS"].split(","),
            "assignPublicIp": "ENABLED",
        }}
        self.launch_config = hashlib.sha256(json.dumps(
            [self.cluster, self.definition, self.network], sort_keys=True).encode()).hexdigest()

    def handle(self, command, token, requested_at=None):
        now = int(time.time())
        row = self.table.get_item(Key=KEY, ConsistentRead=True).get("Item")
        if row and row.get("taskArn"):
            result = self.ecs.describe_tasks(cluster=self.cluster, tasks=[row["taskArn"]])
            tasks = result.get("tasks", [])
            if tasks and tasks[0]["lastStatus"] != "STOPPED":
                if command == "stop":
                    return self.stop(tasks[0], requested_at)
                return self.task_status(tasks[0])
            if not tasks:
                failures = result.get("failures", [])
                if not failures or any(f.get("reason") != "MISSING" for f in failures):
                    return CHECK
                if now - row["createdAt"] < 3600:
                    if command == "stop":
                        return WAIT_TO_STOP
                    return STARTING  # ECS can briefly return MISSING after RunTask.
            if command != "start" or token == row["token"]:
                return STOPPED

        if row and not row.get("taskArn"):
            if now - row["createdAt"] > 2700 or row["launchConfig"] != self.launch_config:
                # An ambiguous launch must be reconciled before the ECS token can expire.
                return REVIEW
            if command == "stop":
                return WAIT_TO_STOP
            if command == "status":
                return STARTING
            return self.launch(row["token"])

        if command != "start":
            return STOPPED
        legacy = self.ecs.describe_services(cluster=self.cluster, services=[self.service])
        if not legacy.get("services"):
            if [f.get("reason") for f in legacy.get("failures", [])] != ["MISSING"]:
                return CHECK
        elif any(legacy["services"][0][key] for key in ("desiredCount", "runningCount", "pendingCount")):
            return "Il vecchio servizio ECS ha ancora repliche attive o richieste."

        condition = (Attr("token").eq(row["token"]) if row else Attr("id").not_exists())
        try:
            self.table.put_item(Item={**KEY, "token": token, "createdAt": now,
                                      "launchConfig": self.launch_config}, ConditionExpression=condition)
        except self.table.meta.client.exceptions.ConditionalCheckFailedException:
            return STARTING
        return self.launch(token)

    def launch(self, token):
        result = self.ecs.run_task(cluster=self.cluster, taskDefinition=self.definition, count=1,
                                   launchType="FARGATE", platformVersion="LATEST",
                                   networkConfiguration=self.network, startedBy=STARTED_BY,
                                   clientToken=token)
        tasks = result.get("tasks", [])
        try:
            if not tasks:
                if not result.get("failures"):
                    return CHECK
                LOG.warning("ECS could not place the task: %s",
                            [failure.get("reason") for failure in result.get("failures", [])])
                self.table.delete_item(Key=KEY, ConditionExpression=Attr("token").eq(token)
                                       & Attr("taskArn").not_exists())
                return "ECS non ha avviato il server. Riprova con un nuovo start."
            self.table.update_item(Key=KEY, UpdateExpression="SET taskArn = :arn",
                                   ExpressionAttributeValues={":arn": tasks[0]["taskArn"]},
                                   ConditionExpression=Attr("token").eq(token))
        except self.table.meta.client.exceptions.ConditionalCheckFailedException:
            # Another invocation already moved the slot forward.
            return CHECK
        return self.task_status(tasks[0])

    def stop(self, task, requested_at):
        if task.get("startedBy") != STARTED_BY or not task.get("createdAt"):
            return CHECK
        # A delayed delivery must never stop a replacement for the original task.
        if task["createdAt"] > requested_at:
            return "Il task è stato avviato dopo questo messaggio. Invia un nuovo stop."
        if task.get("desiredStatus") == "STOPPED":
            return STOPPING
        result = self.ecs.stop_task(cluster=self.cluster, task=task["taskArn"],
                                    reason="Stop requested from Google Chat")
        return STOPPED if result["task"]["lastStatus"] == "STOPPED" else STOPPING

    def task_status(self, task):
        if task["lastStatus"] == "STOPPED":
            return STOPPED
        if task.get("desiredStatus") == "STOPPED":
            return STOPPING
        if task["lastStatus"] != "RUNNING":
            return STARTING
        eni = next((detail["value"] for attachment in task.get("attachments", [])
                    if attachment.get("type") == "ElasticNetworkInterface"
                    for detail in attachment.get("details", [])
                    if detail["name"] == "networkInterfaceId"), None)
        if eni:
            try:
                interfaces = self.ec2.describe_network_interfaces(NetworkInterfaceIds=[eni])
            except ClientError as exc:
                if exc.response["Error"]["Code"] != "InvalidNetworkInterfaceID.NotFound":
                    raise
            else:
                for interface in interfaces.get("NetworkInterfaces", []):
                    address = interface.get("Association", {}).get("PublicIp")
                    if address:
                        return (f"Task in esecuzione. Connessione diretta: {address}:7777. "
                                "Se il gioco non risponde ancora, riprova tra poco.")
        return "Task in esecuzione. IP pubblico in arrivo; riprova con status."


def make_server():
    session = boto3.session.Session()
    table = session.resource("dynamodb", config=CONFIG).Table(os.environ["STATE_TABLE"])
    return GameServer(session.client("ecs", config=CONFIG), session.client("ec2", config=CONFIG),
                      table, os.environ)


SERVER = None


def handler(event, _context):
    request = chat_request(event, set(os.environ["ALLOWED_USERS"].split(",")),
                           os.environ.get("ALLOWED_DOMAIN_ID", ""))
    if isinstance(request, dict):
        return request
    global SERVER
    try:
        if SERVER is None:
            SERVER = make_server()
        return response(SERVER.handle(*request))
    except (ClientError, BotoCoreError) as exc:
        error = exc.response["Error"]["Code"] if isinstance(exc, ClientError) else type(exc).__name__
        LOG.error("AWS request failed: %s (%s)", getattr(exc, "operation_name", None), error)
        if request[0] == "start":
            return response("Avvio non confermato. Controlla con status o riprova con start.")
        if request[0] == "stop":
            return response("Arresto non confermato. Controlla con status o riprova con stop.")
        return response(CHECK)

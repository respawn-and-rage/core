import json
import time
import unittest
from datetime import datetime, timedelta, timezone
from unittest.mock import Mock

from botocore.exceptions import ReadTimeoutError

import app


def event(command, user="users/123"):
    return {
        "requestContext": {"authorizer": {"jwt": {"claims": {
            "email": app.CHAT_SERVICE, "email_verified": "true",
        }}}},
        "body": json.dumps({
            "type": "MESSAGE",
            "user": {"type": "HUMAN", "name": user},
            "space": {"spaceType": "DIRECT_MESSAGE"},
            "message": {"name": "spaces/test/messages/message",
                        "createTime": datetime.now(timezone.utc).isoformat(),
                        "argumentText": command},
        }),
    }


class ChatRequests(unittest.TestCase):
    def test_workspace_access_uses_the_authenticated_user_domain(self):
        request = event("status", user="users/456")
        body = json.loads(request["body"])
        body["user"]["domainId"] = "internal"
        request["body"] = json.dumps(body)
        self.assertEqual(app.chat_request(request, set(), "internal"), ("status", None))
        denied = app.chat_request(request, set(), "other")
        self.assertIn("non è autorizzato", json.loads(denied["body"])["text"])
        del body["user"]["domainId"]
        body["user"]["email"] = "person@example.test"
        request["body"] = json.dumps(body)
        denied = app.chat_request(request, set(), "internal")
        self.assertIn("non è autorizzato", json.loads(denied["body"])["text"])

    def test_untrusted_issuer_and_unlisted_user_cannot_start(self):
        request = event("start")
        request["requestContext"]["authorizer"]["jwt"]["claims"]["email"] = "other@example.test"
        self.assertEqual(app.chat_request(request, {"users/123"})["statusCode"], 401)
        denied = app.chat_request(event("start", "users/456"), {"users/123"})
        self.assertIn("non è autorizzato", json.loads(denied["body"])["text"])

    def test_retry_uses_same_token_and_old_message_is_rejected(self):
        request = event(" START ")
        first = app.chat_request(request, {"users/123"})
        second = app.chat_request(request, {"users/123"})
        self.assertEqual(first, second)
        self.assertEqual(first[0], "start")
        body = json.loads(request["body"])
        body["message"]["createTime"] = (datetime.now(timezone.utc) - timedelta(hours=1)).isoformat()
        request["body"] = json.dumps(body)
        self.assertIn("scaduto", app.chat_request(request, {"users/123"})["body"])

    def test_stop_requires_authorization_and_a_fresh_timestamp(self):
        request = event("STOP")
        self.assertIn("non è autorizzato", json.loads(app.chat_request(request, set())["body"])["text"])
        command, _, created = app.chat_request(request, {"users/123"})
        self.assertEqual(command, "stop")
        self.assertGreater(created, datetime.now(timezone.utc) - timedelta(seconds=5))
        message = json.loads(request["body"])
        message["message"]["createTime"] = (created - timedelta(minutes=15)).isoformat()
        request["body"] = json.dumps(message)
        self.assertIn("scaduto", app.chat_request(request, {"users/123"})["body"])

    def test_whoami_works_before_allowlisting_and_only_discloses_id_in_dm(self):
        request = event("whoami")
        self.assertIn("users/123", app.chat_request(request, set())["body"])
        body = json.loads(request["body"])
        body["space"]["spaceType"] = "SPACE"
        request["body"] = json.dumps(body)
        self.assertNotIn("users/123", app.chat_request(request, set())["body"])


class ServerLifecycle(unittest.TestCase):
    def setUp(self):
        self.ecs, self.ec2, self.table = Mock(), Mock(), Mock()
        self.table.get_item.return_value = {}
        self.ecs.describe_services.return_value = {
            "services": [{"desiredCount": 0, "runningCount": 0, "pendingCount": 0}]}
        self.ecs.run_task.return_value = {"tasks": [{"taskArn": "task", "lastStatus": "PROVISIONING"}]}
        self.server = app.GameServer(self.ecs, self.ec2, self.table, {
            "CLUSTER": "cluster", "LEGACY_SERVICE": "legacy", "TASK_DEFINITION": "fixture:3",
            "SUBNETS": "subnet-1,subnet-2", "SECURITY_GROUPS": "sg-1",
        })

    def test_start_is_single_standalone_fargate_task_using_existing_network(self):
        self.assertEqual(self.server.handle("start", "token"), app.STARTING)
        self.ecs.run_task.assert_called_once()
        args = self.ecs.run_task.call_args.kwargs
        self.assertEqual((args["taskDefinition"], args["count"], args["launchType"]),
                         ("fixture:3", 1, "FARGATE"))
        self.assertEqual(args["networkConfiguration"]["awsvpcConfiguration"], {
            "subnets": ["subnet-1", "subnet-2"], "securityGroups": ["sg-1"], "assignPublicIp": "ENABLED",
        })
        self.ecs.update_service.assert_not_called()

    def test_retry_recovers_ambiguous_run_task_using_original_token(self):
        self.ecs.run_task.side_effect = ReadTimeoutError(endpoint_url="https://example.test")
        with self.assertRaises(ReadTimeoutError):
            self.server.handle("start", "original")
        self.table.get_item.return_value = {"Item": self.table.put_item.call_args.kwargs["Item"]}
        self.ecs.run_task.side_effect = None
        self.assertEqual(self.server.handle("status", None), app.STARTING)
        self.assertEqual(self.server.handle("start", "different"), app.STARTING)
        self.assertEqual([call.kwargs["clientToken"] for call in self.ecs.run_task.call_args_list],
                         ["original", "original"])

    def test_active_server_shares_ip_and_stopped_server_requires_fresh_start(self):
        self.table.get_item.return_value = {"Item": {
            "token": "old", "taskArn": "task", "createdAt": int(time.time()),
        }}
        self.ecs.describe_tasks.return_value = {"tasks": [{
            "lastStatus": "RUNNING", "attachments": [{
                "type": "ElasticNetworkInterface",
                "details": [{"name": "networkInterfaceId", "value": "eni-1"}],
            }],
        }]}
        self.ec2.describe_network_interfaces.return_value = {
            "NetworkInterfaces": [{"Association": {"PublicIp": "203.0.113.25"}}]}
        self.assertIn("203.0.113.25:7777", self.server.handle("start", "different"))
        self.ecs.describe_tasks.return_value = {"tasks": [{"lastStatus": "STOPPED"}]}
        self.assertEqual(self.server.handle("status", None), app.STOPPED)
        self.assertEqual(self.server.handle("start", "old"), app.STOPPED)
        self.ecs.run_task.assert_not_called()
        self.assertEqual(self.server.handle("start", "fresh"), app.STARTING)
        self.ecs.run_task.assert_called_once()

    def test_legacy_replicas_and_expired_unknown_launch_block_start(self):
        self.ecs.describe_services.return_value["services"][0]["desiredCount"] = 1
        self.assertIn("vecchio servizio", self.server.handle("start", "new"))
        self.table.get_item.return_value = {"Item": {
            "token": "old", "createdAt": int(time.time()) - 3600,
        }}
        self.assertEqual(self.server.handle("start", "new"), app.REVIEW)
        self.ecs.run_task.assert_not_called()

    def test_stop_targets_only_the_tracked_task_and_handles_retries(self):
        requested = datetime.now(timezone.utc)
        self.assertEqual(self.server.handle("stop", None, requested), app.STOPPED)
        self.table.get_item.return_value = {"Item": {"taskArn": "task"}}
        task = {"taskArn": "task", "lastStatus": "RUNNING", "desiredStatus": "RUNNING",
                "startedBy": "rr-google-chat", "createdAt": requested - timedelta(seconds=5)}
        self.ecs.describe_tasks.return_value = {"tasks": [task]}
        self.ecs.stop_task.return_value = {"task": {**task, "desiredStatus": "STOPPED"}}
        self.assertEqual(self.server.handle("stop", None, requested), app.STOPPING)
        task["desiredStatus"] = "STOPPED"
        self.assertEqual(self.server.handle("stop", None, requested), app.STOPPING)
        task["lastStatus"] = "STOPPED"
        self.assertEqual(self.server.handle("stop", None, requested), app.STOPPED)
        self.ecs.stop_task.assert_called_once_with(cluster="cluster", task="task",
                                                  reason="Stop requested from Google Chat")
        self.ecs.run_task.assert_not_called()

    def test_delayed_stop_cannot_stop_a_newer_or_unrelated_task(self):
        requested = datetime.now(timezone.utc)
        self.table.get_item.return_value = {"Item": {"taskArn": "task"}}
        task = {"taskArn": "task", "lastStatus": "PENDING", "startedBy": "rr-google-chat",
                "createdAt": requested + timedelta(seconds=1)}
        self.ecs.describe_tasks.return_value = {"tasks": [task]}
        self.assertIn("dopo questo messaggio", self.server.handle("stop", None, requested))
        task["startedBy"] = "ecs-svc/replacement"
        self.assertEqual(self.server.handle("stop", None, requested), app.CHECK)
        self.ecs.stop_task.assert_not_called()

    def test_concurrent_start_losing_conditional_write_does_not_launch(self):
        class ConditionalFailure(Exception):
            pass

        self.table.meta.client.exceptions.ConditionalCheckFailedException = ConditionalFailure
        self.table.put_item.side_effect = ConditionalFailure
        self.assertEqual(self.server.handle("start", "loser"), app.STARTING)
        self.ecs.run_task.assert_not_called()


if __name__ == "__main__":
    unittest.main()

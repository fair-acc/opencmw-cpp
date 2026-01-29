#!/usr/bin/env bash

# This script requires keycloak to be already running and configures it to be compatible with the unit tests by setting up a realm, a user etc...
# To start a keycloak instance in the first place, the following is sufficient:
# docker run -p 8090:8080 -e KEYCLOAK_ADMIN=admin -e KEYCLOAK_ADMIN_PASSWORD=admin quay.io/keycloak/keycloak:25.0.4 start-dev
#
# For more information see: https://www.keycloak.org/getting-started/getting-started-docker

URL="${KEYCLOAK_URL:-http://localhost:8090}"
REDIRECT_URI="${KEYCLOAK_REDIRECT_URI:-http://localhost:8091}"
ADMIN_PASSWORD="${KEYCLOAK_ADMIN_PASSWORD:-admin}"

while ! curl -s "$URL" >/dev/null; do
    echo "Waiting for $URL to be reachable..."
    sleep 1
done

echo 'Getting access token'
AUTH="Authorization: Bearer $(curl -s -X POST "$URL/realms/master/protocol/openid-connect/token" -H 'Accept: application/json' -H 'Content-Type: application/x-www-form-urlencoded' -d 'grant_type=password&username=admin&password='"$ADMIN_PASSWORD"'&client_id=admin-cli' | jq -r '.access_token')"

echo 'Creating realm with name "testrealm"'
curl -s -X POST "$URL/admin/realms" -H 'Content-Type: application/json' -H "$AUTH" -d '{"realm":"testrealm","enabled":true}'

echo 'Creating user with name "testuser" and password "testuser"'
curl -s -X POST "$URL/admin/realms/testrealm/users" -H 'Content-Type: application/json' -H "$AUTH" -d '{"username":"testuser","emailVerified":true,"enabled":true,"firstName":"Mr.","lastName":"Bar","email":"foo@bar.com","credentials":[{"type":"password","temporary":false,"value":"testuser"}]}'

echo 'Adding a client with client id "testclientid"'
curl -s -X POST "$URL/admin/realms/testrealm/clients" -H 'Content-Type: application/json' -H "$AUTH" -d '{"clientId":"testclientid","enabled":true,"redirectUris":["'"$REDIRECT_URI"'"],"publicClient":true}'

echo 'Adding a confidential client with client id "confidentialclientid" and secret "secret00000000000000000000000000"'
curl -s -X POST "$URL/admin/realms/testrealm/clients" -H 'Content-Type: application/json' -H "$AUTH" -d '{"clientId":"confidentialclientid","enabled":true,"redirectUris":["'"$REDIRECT_URI"'"],"publicClient":false,"clientAuthenticatorType":"client-secret","directAccessGrantsEnabled":true,"secret":"secret00000000000000000000000000"}'

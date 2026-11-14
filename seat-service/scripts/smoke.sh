#!/usr/bin/env bash
# End-to-end smoke test against the local dev server: leader subscribes, makes a team + invite, member joins, gets a token.
set -euo pipefail
B=${B:-http://localhost:8787}; j() { node -e "let s='';process.stdin.on('data',d=>s+=d).on('end',()=>console.log(JSON.parse(s)$1))"; }
LT=$(curl -s "$B/dev/siwa?sub=leader" | j .identityToken)
LS=$(curl -s -XPOST "$B/v1/auth/apple" -d "{\"identityToken\":\"$LT\"}" | j .session)
TX=$(curl -s "$B/dev/transaction?otid=OT1&days=30" | j .signedTransaction)
curl -sf -XPOST "$B/v1/teams" -H "authorization: Bearer $LS" -d "{\"churchName\":\"Grace Chapel\",\"displayName\":\"Clayton\",\"signedTransaction\":\"$TX\"}" >/dev/null
CODE=$(curl -s -XPOST "$B/v1/teams/me/invites" -H "authorization: Bearer $LS" | j .code)
MT=$(curl -s "$B/dev/siwa?sub=drummer" | j .identityToken)
MS=$(curl -s -XPOST "$B/v1/auth/apple" -d "{\"identityToken\":\"$MT\"}" | j .session)
curl -sf -XPOST "$B/v1/invites/redeem" -H "authorization: Bearer $MS" -d "{\"code\":\"$CODE\",\"displayName\":\"Drummer\"}" >/dev/null
curl -s -XPOST "$B/v1/entitlement" -H "authorization: Bearer $MS" | j .status
curl -s "$B/v1/teams/me" -H "authorization: Bearer $LS" | j ".members.length"
echo "smoke OK (invite $CODE)"

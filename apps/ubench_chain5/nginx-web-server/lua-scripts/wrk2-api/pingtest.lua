local socket = require "socket"
local _M = {}
local k8s_suffix = os.getenv("fqdn_suffix")
if (k8s_suffix == nil) then
  k8s_suffix = ""
end

local function _StrIsEmpty(s)
  return s == nil or s == ''
end

function _M.Ping()
  local ngx = ngx
  local GenericObjectPool = require "GenericObjectPool"
  local UserTimelineServiceClient = require "social_network_PingService"
  local cjson = require "cjson"
  local liblualongnumber = require "liblualongnumber"
  local req_id = tonumber(string.sub(ngx.var.request_id, 0, 15), 16)

  local timestamp = socket.gettime()*1000
  local client = GenericObjectPool:connection(
      UserTimelineServiceClient, "ping-service-0" .. k8s_suffix, 5000)
  ngx.say("Sent client ping")
  local status, ret = pcall(client.Ping, client, req_id, timestamp, 0)
  ngx.say("Received client response: ")
  
  if not status then
    ngx.status = ngx.HTTP_INTERNAL_SERVER_ERROR
    if (ret.message) then
      ngx.say("Get ping failure: " .. ret.message)
      ngx.log(ngx.ERR, "Get ping failure: " .. ret.message)
    else
      ngx.say("Get ping failure: " .. ret)
      ngx.log(ngx.ERR, "Get ping failure: " .. ret)
    end
    client.iprot.trans:close()
    ngx.exit(ngx.HTTP_INTERNAL_SERVER_ERROR)
  else
    GenericObjectPool:returnConnection(client)
    --local user_timeline = _LoadTimeline(ret)
    --ngx.header.content_type = "application/json; charset=utf-8"
    --ngx.say(cjson.encode(user_timeline) )

  end
  ngx.exit(ngx.HTTP_OK)
end

return _M

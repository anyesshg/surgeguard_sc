---- Licensed to the Apache Software Foundation (ASF) under one
-- or more contributor license agreements. See the NOTICE file
-- distributed with this work for additional information
-- regarding copyright ownership. The ASF licenses this file
-- to you under the Apache License, Version 2.0 (the
-- "License"); you may not use this file except in compliance
-- with the License. You may obtain a copy of the License at
--
--   http://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing,
-- software distributed under the License is distributed on an
-- "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
-- KIND, either express or implied. See the License for the
-- specific language governing permissions and limitations
-- under the License.
--
require 'Thrift'

TTransportException = TException:new {
  UNKNOWN             = 0,
  NOT_OPEN            = 1,
  ALREADY_OPEN        = 2,
  TIMED_OUT           = 3,
  END_OF_FILE         = 4,
  INVALID_FRAME_SIZE  = 5,
  INVALID_TRANSFORM   = 6,
  INVALID_CLIENT_TYPE = 7,
  errorCode        = 0,
  __type = 'TTransportException'
}

local TSocket = __TObject:new({
  __type = 'TSocket',
  timeout = 1000,
  host = 'localhost',
  port = 9090,
  handle
})

function TSocket:create(obj)
  return __TObject.new(self, obj)
end

function TSocket:readAll(len)
  local buf, have, chunk = '', 0
  while have < len do
    chunk = self:read(len - have)
    have = have + string.len(chunk)
    buf = buf .. chunk

    if string.len(chunk) == 0 then
      terror(TTransportException:new{
        errorCode = TTransportException.END_OF_FILE
      })
    end
  end
  return buf
end

function TSocket:close()
  if self.handle then
    self.handle:close()
    self.handle = nil
  end
end

function TSocket:setKeepAlive(timeout, size)
  if self.handle then
    self.handle:setkeepalive(timeout, size)
  end
end

-- Returns a table with the fields host and port
function TSocket:getSocketInfo()
  if self.handle then
    return self.handle:getsockinfo()
  end
  terror(TTransportException:new{errorCode = TTransportException.NOT_OPEN})
end

function TSocket:setTimeout(timeout)
  if timeout and ttype(timeout) == 'number' then
    if self.handle then
      self.handle:settimeout(timeout)
    end
    self.timeout = timeout
  end
end

function TSocket:isOpen()
  if self.handle then
    return true
  end
  return false
end

function TSocket:open()
  if not self.handle then
    -- Use NGINX socket instead of the built-in lua socket
    self.handle = ngx.socket.tcp()
    self.handle:settimeout(self.timeout)
  end
  local ok, err = self.handle:connect(self.host, self.port)
  if not ok then
    terror(TTransportException:new{
      message = 'Could not connect to ' .. self.host .. ':' .. self.port
        .. ' (' .. err .. ')'
    })
  end
end

function TSocket:read(len)
  local buf = self.handle:receive(len)
  if not buf or string.len(buf) ~= len then
    terror(TTransportException:new{errorCode = TTransportException.UNKNOWN})
  end
  return buf
end

function TSocket:write(buf)
  self.handle:send(buf)
end

function TSocket:flush()
end

-- TServerSocket
-- TServerSocket = TSocket:new{
--   __type = 'TServerSocket',
--   host = 'localhost',
--   port = 9090
-- }
-- 
-- function TServerSocket:listen()
--   if self.handle then
--     self:close()
--   end
-- 
--   local sock, err = luasocket.create(self.host, self.port)
--   if not err then
--     self.handle = sock
--   else
--     terror(err)
--   end
--   self.handle:settimeout(self.timeout)
--   self.handle:listen()
-- end
-- 
-- function TServerSocket:accept()
--   local client, err = self.handle:accept()
--   if err then
--     terror(err)
--   end
--   return TSocket:new({handle = client})
-- end

return TSocket

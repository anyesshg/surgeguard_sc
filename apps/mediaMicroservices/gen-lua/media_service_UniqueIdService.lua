--
-- Autogenerated by Thrift
--
-- DO NOT EDIT UNLESS YOU ARE SURE THAT YOU KNOW WHAT YOU ARE DOING
-- @generated
--


require 'Thrift'
require 'media_service_ttypes'

UniqueIdServiceClient = __TObject.new(__TClient, {
  __type = 'UniqueIdServiceClient'
})

function UniqueIdServiceClient:UploadUniqueId(req_id, carrier)
  self:send_UploadUniqueId(req_id, carrier)
  self:recv_UploadUniqueId(req_id, carrier)
end

function UniqueIdServiceClient:send_UploadUniqueId(req_id, carrier)
  self.oprot:writeMessageBegin('UploadUniqueId', TMessageType.CALL, self._seqid)
  local args = UploadUniqueId_args:new{}
  args.req_id = req_id
  args.carrier = carrier
  args:write(self.oprot)
  self.oprot:writeMessageEnd()
  self.oprot.trans:flush()
end

function UniqueIdServiceClient:recv_UploadUniqueId(req_id, carrier)
  local fname, mtype, rseqid = self.iprot:readMessageBegin()
  if mtype == TMessageType.EXCEPTION then
    local x = TApplicationException:new{}
    x:read(self.iprot)
    self.iprot:readMessageEnd()
    error(x)
  end
  local result = UploadUniqueId_result:new{}
  result:read(self.iprot)
  self.iprot:readMessageEnd()
end
UniqueIdServiceIface = __TObject:new{
  __type = 'UniqueIdServiceIface'
}


UniqueIdServiceProcessor = __TObject.new(__TProcessor
, {
 __type = 'UniqueIdServiceProcessor'
})

function UniqueIdServiceProcessor:process(iprot, oprot, server_ctx)
  local name, mtype, seqid = iprot:readMessageBegin()
  local func_name = 'process_' .. name
  if not self[func_name] or ttype(self[func_name]) ~= 'function' then
    iprot:skip(TType.STRUCT)
    iprot:readMessageEnd()
    x = TApplicationException:new{
      errorCode = TApplicationException.UNKNOWN_METHOD
    }
    oprot:writeMessageBegin(name, TMessageType.EXCEPTION, seqid)
    x:write(oprot)
    oprot:writeMessageEnd()
    oprot.trans:flush()
  else
    self[func_name](self, seqid, iprot, oprot, server_ctx)
  end
end

function UniqueIdServiceProcessor:process_UploadUniqueId(seqid, iprot, oprot, server_ctx)
  local args = UploadUniqueId_args:new{}
  local reply_type = TMessageType.REPLY
  args:read(iprot)
  iprot:readMessageEnd()
  local result = UploadUniqueId_result:new{}
  local status, res = pcall(self.handler.UploadUniqueId, self.handler, args.req_id, args.carrier)
  if not status then
    reply_type = TMessageType.EXCEPTION
    result = TApplicationException:new{message = res}
  elseif ttype(res) == 'ServiceException' then
    result.se = res
  else
    result.success = res
  end
  oprot:writeMessageBegin('UploadUniqueId', reply_type, seqid)
  result:write(oprot)
  oprot:writeMessageEnd()
  oprot.trans:flush()
end

-- HELPER FUNCTIONS AND STRUCTURES

UploadUniqueId_args = __TObject:new{
  req_id,
  carrier
}

function UploadUniqueId_args:read(iprot)
  iprot:readStructBegin()
  while true do
    local fname, ftype, fid = iprot:readFieldBegin()
    if ftype == TType.STOP then
      break
    elseif fid == 1 then
      if ftype == TType.I64 then
        self.req_id = iprot:readI64()
      else
        iprot:skip(ftype)
      end
    elseif fid == 2 then
      if ftype == TType.MAP then
        self.carrier = {}
        local _ktype37, _vtype38, _size36 = iprot:readMapBegin() 
        for _i=1,_size36 do
          local _key40 = iprot:readString()
          local _val41 = iprot:readString()
          self.carrier[_key40] = _val41
        end
        iprot:readMapEnd()
      else
        iprot:skip(ftype)
      end
    else
      iprot:skip(ftype)
    end
    iprot:readFieldEnd()
  end
  iprot:readStructEnd()
end

function UploadUniqueId_args:write(oprot)
  oprot:writeStructBegin('UploadUniqueId_args')
  if self.req_id ~= nil then
    oprot:writeFieldBegin('req_id', TType.I64, 1)
    oprot:writeI64(self.req_id)
    oprot:writeFieldEnd()
  end
  if self.carrier ~= nil then
    oprot:writeFieldBegin('carrier', TType.MAP, 2)
    oprot:writeMapBegin(TType.STRING, TType.STRING, ttable_size(self.carrier))
    for kiter42,viter43 in pairs(self.carrier) do
      oprot:writeString(kiter42)
      oprot:writeString(viter43)
    end
    oprot:writeMapEnd()
    oprot:writeFieldEnd()
  end
  oprot:writeFieldStop()
  oprot:writeStructEnd()
end

UploadUniqueId_result = __TObject:new{
  se
}

function UploadUniqueId_result:read(iprot)
  iprot:readStructBegin()
  while true do
    local fname, ftype, fid = iprot:readFieldBegin()
    if ftype == TType.STOP then
      break
    elseif fid == 1 then
      if ftype == TType.STRUCT then
        self.se = ServiceException:new{}
        self.se:read(iprot)
      else
        iprot:skip(ftype)
      end
    else
      iprot:skip(ftype)
    end
    iprot:readFieldEnd()
  end
  iprot:readStructEnd()
end

function UploadUniqueId_result:write(oprot)
  oprot:writeStructBegin('UploadUniqueId_result')
  if self.se ~= nil then
    oprot:writeFieldBegin('se', TType.STRUCT, 1)
    self.se:write(oprot)
    oprot:writeFieldEnd()
  end
  oprot:writeFieldStop()
  oprot:writeStructEnd()
end
return UniqueIdServiceClient, UniqueIdServiceProcessor
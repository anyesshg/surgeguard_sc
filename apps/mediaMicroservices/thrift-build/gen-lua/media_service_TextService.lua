--
-- Autogenerated by Thrift
--
-- DO NOT EDIT UNLESS YOU ARE SURE THAT YOU KNOW WHAT YOU ARE DOING
-- @generated
--


require 'Thrift'
require 'media_service_ttypes'

TextServiceClient = __TObject.new(__TClient, {
  __type = 'TextServiceClient'
})

function TextServiceClient:UploadText(req_id, text, carrier)
  self:send_UploadText(req_id, text, carrier)
  self:recv_UploadText(req_id, text, carrier)
end

function TextServiceClient:send_UploadText(req_id, text, carrier)
  self.oprot:writeMessageBegin('UploadText', TMessageType.CALL, self._seqid)
  local args = UploadText_args:new{}
  args.req_id = req_id
  args.text = text
  args.carrier = carrier
  args:write(self.oprot)
  self.oprot:writeMessageEnd()
  self.oprot.trans:flush()
end

function TextServiceClient:recv_UploadText(req_id, text, carrier)
  local fname, mtype, rseqid = self.iprot:readMessageBegin()
  if mtype == TMessageType.EXCEPTION then
    local x = TApplicationException:new{}
    x:read(self.iprot)
    self.iprot:readMessageEnd()
    error(x)
  end
  local result = UploadText_result:new{}
  result:read(self.iprot)
  self.iprot:readMessageEnd()
end
TextServiceIface = __TObject:new{
  __type = 'TextServiceIface'
}


TextServiceProcessor = __TObject.new(__TProcessor
, {
 __type = 'TextServiceProcessor'
})

function TextServiceProcessor:process(iprot, oprot, server_ctx)
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

function TextServiceProcessor:process_UploadText(seqid, iprot, oprot, server_ctx)
  local args = UploadText_args:new{}
  local reply_type = TMessageType.REPLY
  args:read(iprot)
  iprot:readMessageEnd()
  local result = UploadText_result:new{}
  local status, res = pcall(self.handler.UploadText, self.handler, args.req_id, args.text, args.carrier)
  if not status then
    reply_type = TMessageType.EXCEPTION
    result = TApplicationException:new{message = res}
  elseif ttype(res) == 'ServiceException' then
    result.se = res
  else
    result.success = res
  end
  oprot:writeMessageBegin('UploadText', reply_type, seqid)
  result:write(oprot)
  oprot:writeMessageEnd()
  oprot.trans:flush()
end

-- HELPER FUNCTIONS AND STRUCTURES

UploadText_args = __TObject:new{
  req_id,
  text,
  carrier
}

function UploadText_args:read(iprot)
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
      if ftype == TType.STRING then
        self.text = iprot:readString()
      else
        iprot:skip(ftype)
      end
    elseif fid == 3 then
      if ftype == TType.MAP then
        self.carrier = {}
        local _ktype61, _vtype62, _size60 = iprot:readMapBegin() 
        for _i=1,_size60 do
          local _key64 = iprot:readString()
          local _val65 = iprot:readString()
          self.carrier[_key64] = _val65
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

function UploadText_args:write(oprot)
  oprot:writeStructBegin('UploadText_args')
  if self.req_id ~= nil then
    oprot:writeFieldBegin('req_id', TType.I64, 1)
    oprot:writeI64(self.req_id)
    oprot:writeFieldEnd()
  end
  if self.text ~= nil then
    oprot:writeFieldBegin('text', TType.STRING, 2)
    oprot:writeString(self.text)
    oprot:writeFieldEnd()
  end
  if self.carrier ~= nil then
    oprot:writeFieldBegin('carrier', TType.MAP, 3)
    oprot:writeMapBegin(TType.STRING, TType.STRING, ttable_size(self.carrier))
    for kiter66,viter67 in pairs(self.carrier) do
      oprot:writeString(kiter66)
      oprot:writeString(viter67)
    end
    oprot:writeMapEnd()
    oprot:writeFieldEnd()
  end
  oprot:writeFieldStop()
  oprot:writeStructEnd()
end

UploadText_result = __TObject:new{
  se
}

function UploadText_result:read(iprot)
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

function UploadText_result:write(oprot)
  oprot:writeStructBegin('UploadText_result')
  if self.se ~= nil then
    oprot:writeFieldBegin('se', TType.STRUCT, 1)
    self.se:write(oprot)
    oprot:writeFieldEnd()
  end
  oprot:writeFieldStop()
  oprot:writeStructEnd()
end
return TextServiceClient, TextServiceProcessor
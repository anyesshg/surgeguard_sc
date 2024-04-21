local max_user_index = tonumber(os.getenv("max_user_index")) or 900

request = function()
  local method = "GET"
  local headers = {}
  headers["Content-Type"] = "application/x-www-form-urlencoded"
  local path = "http://localhost:8080/wrk2-api/pingtest"
  return wrk.format(method, path, headers, nil)

end

package = "lua-websockets"
version = "scm-1"

source = {
  url = "git://github.com/lipp/lua-websockets.git",
}

description = {
  summary = "Websockets for Lua",
  homepage = "http://github.com/lipp/lua-websockets",
  license = "MIT/X11",
  detailed = "Provides sync and async clients and servers for copas and lua-ev."
}

dependencies = {
  "lua >= 5.1",
  "struct",
  "luasocket",
  "luabitop",
}

build = {
  type = 'none',
  install = {
    lua = {
      ['websocket'] = 'src/websocket.lua',
      ['websocket.sync'] = 'src/websocket/sync.lua',
      ['websocket.client'] = 'src/websocket/client.lua',
      ['websocket.server'] = 'src/websocket/server.lua',
      ['websocket.handshake'] = 'src/websocket/handshake.lua',
      ['websocket.tools'] = 'src/websocket/tools.lua',
      ['websocket.frame'] = 'src/websocket/frame.lua',
      ['websocket.bit'] = 'src/websocket/bit.lua',
    }
  }
}


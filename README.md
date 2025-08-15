# Tibia 7.7 Login Server
This is a simple login server designed to support [Tibia Game Server](https://github.com/fusion32/tibia-game).

## Compiling
Even though there are no Linux specific features being used, it will currently only compile on Linux. It should be simple enough to support compiling on Windows but I don't think it would add any value, considering the querymanager will be running on Linux and that they need to be both on the same machine. The makefile is very simple and should work as long as OpenSSL's libcrypto, which is the only dependency, is installed. You can add the `-j N` switch to make it compile across N processes.
```
make                # build in release mode
make DEBUG=1        # build in debug mode
make clean          # remove `build` directory
```

## Running
Similar to the game server, the login server won't boot up if it's not able to connect to the [Query Manager](https://github.com/fusion32/tibia-querymanager). That said, running it is straighforward, requiring only the RSA private key `tibia.pem` and `config.cfg` files to be in the working directory. It is always recommended that the server is setup as a service. There is a *systemd* configuration file (`tibia-login.service`) in the repository that may be used for that purpose. The process is very similar to the one described in the [Game Server](https://github.com/fusion32/tibia-game) so I won't repeat myself here.

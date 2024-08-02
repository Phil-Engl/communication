# communication
Very simple reliable chat application built on UDP

# Usage:
1) use "make"
2) ./reliable "port" -w "window-size" "receiver":"port"
    e.g:
            ./reliable 6666 -w 100 localhost:5555 
            ./reliable 5555 -w 100 localhost:6666 (in second terminal)
            
3) input messages and user enter to send

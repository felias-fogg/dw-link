# Wish list for dw-link

* a mode where reads & writes are double-checked: "monitor
  verify/noverify". Could be on or off by default.
* implement simple ISP programmer (again)
* using unused serial interface on Arduino Mega and Leonardo boards
* recovery for RAM and flash read and flash programming errors --> in
  case of timeout when reading, issue DW command again
* count the number of timeouts and make them visible with a monitor
  command
* only get PC when stopped and show in T-record, this could speed up
  conditional breakpoints a lot  

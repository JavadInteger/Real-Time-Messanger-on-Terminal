# Real-Time Messanger - Terminal
A Real-Time Messanger on your Terminal with C++/Asio

## Build
 1- Download and install Asio (standalone)
 2- Add folder 'include/asio' and 'include/asio.hpp' to the root folder
 3- Add this libraries to Linker Settings (If you are Windows):
  ws2_32
	mswsock
  advapi32
 If Linux:
  pthread
 4- Add Root folder to the 'Search Directories'
 5- Add the following macro to the 'Compiler Settings -> #defines':
  ASIO_STANDALONE

If you don't want to do all of the steps, install the pre built version of Messanger

I hope you enjoy

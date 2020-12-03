// $Id: cxi.cpp,v 1.1 2020-11-22 16:51:43-08 - - $

#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
using namespace std;

#include <libgen.h>
#include <sys/types.h>
#include <unistd.h>
#include <fstream>

#include "protocol.h"
#include "logstream.h"
#include "sockets.h"

logstream outlog (cout);
struct cxi_exit: public exception {};

unordered_map<string,cxi_command> command_map {
   {"exit", cxi_command::EXIT},
   {"help", cxi_command::HELP},
   {"ls"  , cxi_command::LS  },
   {"rm"  , cxi_command::RM  },
   {"put" , cxi_command::PUT },
   {"get" , cxi_command::GET },
};

static const char help[] = R"||(
exit         - Exit the program.  Equivalent to EOF.
get filename - Copy remote file to local host.
help         - Print help summary.
ls           - List names of files on remote server.
put filename - Copy local file to remote host.
rm filename  - Remove file from remote server.
)||";

void cxi_help() {
   cout << help;
}

void cxi_ls (client_socket& server) {
   cxi_header header;
   header.command = cxi_command::LS;
   outlog << "sending header " << header << endl;
   send_packet (server, &header, sizeof header);
   recv_packet (server, &header, sizeof header);
   outlog << "received header " << header << endl;
   if (header.command != cxi_command::LSOUT) {
      outlog << "sent LS, server did not return LSOUT" << endl;
      outlog << "server returned " << header << endl;
   }else {
      size_t host_nbytes = ntohl (header.nbytes);
      auto buffer = make_unique<char[]> (host_nbytes + 1);
      recv_packet (server, buffer.get(), host_nbytes);
      outlog << "received " << host_nbytes << " bytes" << endl;
      buffer[host_nbytes] = '\0';
      cout << buffer.get();
   }
}

void cxi_rm (client_socket& server, string filename)
{
   cxi_header header;
   header.command = cxi_command::RM;
   strcpy(header.filename, filename.c_str());
   outlog << "sending header " << header << endl;
   send_packet (server, &header, sizeof header);
   recv_packet (server, &header, sizeof header);
   outlog << "received header " << header << endl;
   if (header.command != cxi_command::ACK) {
      outlog << "sent RM, server did not return ACK" << endl;
      outlog << filename << ": No such file exists" << endl;
      outlog << "server returned " << header << endl;
   }
   else
   {
      outlog << "file removed successfully" << endl;
   }
}

void cxi_put(client_socket& server, string filename)
{
   cxi_header header;
   header.command = cxi_command::PUT;
   strcpy(header.filename, filename.c_str());
   
   ifstream file(filename, std::ifstream::binary);
   file.seekg(0, file.end);
   int length = file.tellg();
   file.seekg(0, file.beg);
   char buffer[length];
   file.read(buffer, length);
   header.nbytes = length;
   
   outlog << "sending header " << header << endl;
   send_packet (server, &header, sizeof header);
   recv_packet (server, &header, sizeof header);
   outlog << "received header " << header << endl;

   send_packet(server, buffer, header.nbytes);
   recv_packet(server, &header, sizeof header);
   if (header.command != cxi_command::ACK) {
      outlog << "sent RM, server did not return ACK" << endl;
      outlog << filename << ": No such file exists" << endl;
      outlog << "server returned " << header << endl;
   }
   else
   {
      outlog << "file sent successfully" << endl;
   }
}

void cxi_get(client_socket& server, string filename)
{
   cxi_header header;
   header.command = cxi_command::GET;
   strcpy(header.filename, filename.c_str());
   outlog << "sending header" << endl;
   send_packet(server, &header, sizeof header);
   recv_packet(server, &header, sizeof header);
   outlog << "received header " << header << endl;
   char buffer[header.nbytes];
   recv_packet(server, buffer, header.nbytes);

   if(header.command == cxi_command::FILEOUT)
   {
      outlog << "file retrieved successfully" << endl;
   }
   ofstream file (header.filename, std::ofstream::binary);
   file.write(buffer, header.nbytes);
}

void usage() {
   cerr << "Usage: " << outlog.execname() << " [host] [port]" << endl;
   throw cxi_exit();
}

vector<string> split (const string& line, const string& delimiters) {
   vector<string> words;
   size_t end = 0;

   for (;;) 
   {
      size_t start = line.find_first_not_of (delimiters, end);
      if (start == string::npos) break;
      end = line.find_first_of (delimiters, start);
      words.push_back (line.substr (start, end - start));
   }
   return words;
}

int main (int argc, char** argv) {
   outlog.execname (basename (argv[0]));
   outlog << "starting" << endl;
   vector<string> args (&argv[1], &argv[argc]);
   if (args.size() > 2) usage();
   string host = get_cxi_server_host (args, 0);
   in_port_t port = get_cxi_server_port (args, 1);
   outlog << to_string (hostinfo()) << endl;
   try {
      outlog << "connecting to " << host << " port " << port << endl;
      client_socket server (host, port);
      outlog << "connected to " << to_string (server) << endl;
      for (;;) {
         string line;
         getline (cin, line);
         if (cin.eof()) throw cxi_exit();
         outlog << "command " << line << endl;
         vector<string> words = split(line, " \t");
         const auto& itor = command_map.find (words.at(0));
         cxi_command cmd = itor == command_map.end()
                         ? cxi_command::ERROR : itor->second;
         switch (cmd) {
            case cxi_command::EXIT:
               throw cxi_exit();
               break;
            case cxi_command::HELP:
               cxi_help();
               break;
            case cxi_command::LS:
               cxi_ls (server);
               break;
            case cxi_command::RM:
               cxi_rm (server, words.at(1));
               break;
            case cxi_command::PUT:
               cxi_put (server, words.at(1));
               break;
            case cxi_command::GET:
               cxi_get (server, words.at(1));
               break;
            default:
               outlog << line << ": invalid command" << endl;
               break;
         }
      }
   }catch (socket_error& error) {
      outlog << error.what() << endl;
   }catch (cxi_exit& error) {
      outlog << "caught cxi_exit" << endl;
   }
   outlog << "finishing" << endl;
   return 0;
}


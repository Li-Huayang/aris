﻿#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <stdint.h>
#include <new>

#ifdef WIN32
#include <ws2tcpip.h>
#endif

#ifdef UNIX
#include<pthread.h>
#include<semaphore.h>
#include<netdb.h>
#include<unistd.h>
#include<arpa/inet.h>
#endif

#include "aris_core_socket.h"

namespace aris
{
	namespace core
	{
		struct Socket::Imp
		{
			Socket* socket;
			
			int lisn_socket_, conn_socket_;  //也可以用SOCKET类型
			struct sockaddr_in server_addr_, client_addr_;
			socklen_t sin_size_;
			Socket::State state_;

			std::function<aris::core::Msg(Socket *, aris::core::Msg &)> onReceivedRequest;
			std::function<int(Socket *, aris::core::Msg &)> onReceivedData;
			std::function<int(Socket *, const char *, int)> onReceivedConnection;
			std::function<int(Socket *)> onLoseConnection;

			std::function<void(Socket *)> onAcceptError;
			std::function<void(Socket *)> onReceiveError;
			

			// 线程同步变量 //
			std::recursive_mutex state_mutex_;

			std::thread recv_data_thread_, recv_conn_thread_;
			std::mutex close_mutex_, cv_mutex_;
			std::condition_variable cv_;

			std::condition_variable_any cv_reply_data_received_;
			aris::core::Msg reply_msg_;
			
			// 连接的socket //
#ifdef WIN32
			WSADATA wsa_data_;             //windows下才用，linux下无该项
#endif
			Imp() : lisn_socket_(0), conn_socket_(0), sin_size_(sizeof(struct sockaddr_in)), state_(Socket::IDLE)
				, onReceivedRequest(nullptr), onReceivedData(nullptr), onReceivedConnection(nullptr), onLoseConnection(nullptr) {}

			~Imp() = default;

			enum MsgType
			{
				SOCKET_GENERAL_DATA,
				SOCKET_REQUEST,
				SOCKET_REPLY
			};

			static void receiveThread(Socket::Imp* imp);
			static void acceptThread(Socket::Imp* imp);
		};
		
		auto Socket::Imp::acceptThread(Socket::Imp* imp)->void
		{
			int lisn_sock,conn_sock;
			struct sockaddr_in client_addr;
			socklen_t sin_size; 

			// 以下从对象中copy内容，此时start_Server在阻塞，因此Socket内部数据安全， //
			// 拷贝好后告诉start_Server函数已经拷贝好 //
			lisn_sock = imp->lisn_socket_;
			client_addr = imp->client_addr_;
			sin_size = imp->sin_size_;

			imp->state_ = WAITING_FOR_CONNECTION;
			
			// 通知主线程，accept线程已经拷贝完毕，准备监听 //
			std::unique_lock<std::mutex> cv_lck(imp->cv_mutex_);
			imp->cv_.notify_one();
			cv_lck.unlock();
			cv_lck.release();

			// 服务器阻塞,直到客户程序建立连接 //
			conn_sock = accept(lisn_sock, (struct sockaddr *)(&client_addr), &sin_size);
			

			// 检查是否正在Close，如果不能锁住，则证明正在close，于是结束线程释放资源 //
			std::unique_lock<std::mutex> cls_lck(imp->close_mutex_, std::defer_lock);
			if (!cls_lck.try_lock())return;

			std::unique_lock<std::recursive_mutex> lck(imp->state_mutex_);
			cls_lck.unlock();
			cls_lck.release();

			// 否则，开始开启数据线程 //
			if (conn_sock == -1)
			{
				imp->socket->stop();

				if (imp->onAcceptError)	imp->onAcceptError(imp->socket);

				return;
			}
			

			// 创建线程 //
			imp->state_ = Socket::WORKING;
			imp->conn_socket_ = conn_sock;
			imp->client_addr_ = client_addr;

			cv_lck = std::unique_lock<std::mutex>(imp->cv_mutex_);
			imp->recv_data_thread_ = std::thread(receiveThread, imp);
			imp->cv_.wait(cv_lck);


			if (imp->onReceivedConnection != nullptr)
			{
				imp->onReceivedConnection(imp->socket, inet_ntoa(imp->client_addr_.sin_addr), ntohs(imp->client_addr_.sin_port));
			}

			return;
		}
		auto Socket::Imp::receiveThread(Socket::Imp* imp)->void
		{
			union Head
			{
				MsgHeader msgHeader;
				char header[sizeof(MsgHeader)];
			} head;
			aris::core::Msg receivedData;
			
			int connSocket = imp->conn_socket_;

			// 通知accept线程已经准备好，下一步开始收发数据 //
			std::unique_lock<std::mutex> cv_lck(imp->cv_mutex_);
			imp->cv_.notify_one();
			cv_lck.unlock();
			cv_lck.release();

			// 开启接受数据的循环 //
			for (;;)
			{
				int res = recv(connSocket, head.header, sizeof(MsgHeader), 0);

				// 检查是否正在Close，如果不能锁住，则证明正在close，于是结束线程释放资源， //
				// 若能锁住，则开始获取Imp所有权 //
				std::unique_lock<std::mutex> close_lck(imp->close_mutex_, std::defer_lock);
				if (!close_lck.try_lock())
				{
					return;
				}

				// 证明没有在close，于是正常接收消息头 //
				std::unique_lock<std::recursive_mutex> state_lck(imp->state_mutex_);
				close_lck.unlock();
				close_lck.release();
				
				if (res <= 0)
				{
					imp->socket->stop();

					if (imp->onLoseConnection != 0)
						imp->onLoseConnection(imp->socket);

					return;
				}

				// 接收消息本体 //
				receivedData.resize(head.msgHeader.msg_size_);
				memcpy(receivedData.data_, head.header, sizeof(MsgHeader));

				if (receivedData.size()>0)
					res = recv(connSocket, receivedData.data(), receivedData.size(), 0);

				if (res <= 0)
				{
					imp->socket->stop();

					if (imp->onLoseConnection != nullptr)
						imp->onLoseConnection(imp->socket);

					return;
				}

				// 根据消息type来确定消息类型 //
				switch (head.msgHeader.msg_type_)
				{
				case SOCKET_GENERAL_DATA:
					if (imp->onReceivedData )imp->onReceivedData(imp->socket, receivedData);
					break;
				case SOCKET_REQUEST:
				{
					aris::core::Msg m;
					if (imp->onReceivedRequest)m = imp->onReceivedRequest(imp->socket, receivedData);

					m.setType(SOCKET_REPLY);

					if (send(imp->conn_socket_, m.data_, m.size() + sizeof(MsgHeader), 0) == -1)
					{
						imp->socket->stop();
						if (imp->onLoseConnection != nullptr)imp->onLoseConnection(imp->socket);
						return;
					}
					break;
				}
				case SOCKET_REPLY:
					if (imp->state_ != WAITING_FOR_REPLY)
					{
						if (imp->onReceiveError)imp->onReceiveError(imp->socket);
						return;
					}
					else
					{
						imp->reply_msg_.swap(receivedData);
						imp->cv_reply_data_received_.notify_one();
					}

					break;
				}
			}		
		}

		Socket::Socket():imp_(new Imp)
		{
			imp_->socket = this;
		}
		Socket::~Socket()
		{
			stop();
		}

		auto Socket::stop()->void
		{
			std::lock(imp_->state_mutex_, imp_->close_mutex_);
			std::unique_lock<std::recursive_mutex> lck1(imp_->state_mutex_, std::adopt_lock);
			std::unique_lock<std::mutex> lck2(imp_->close_mutex_, std::adopt_lock);
			
			switch (imp_->state_)
			{
			case IDLE:
				return;
			case WAITING_FOR_CONNECTION:
#ifdef WIN32
				shutdown(imp_->lisn_socket_, 2);
				closesocket(imp_->lisn_socket_);
				WSACleanup();
#endif
#ifdef UNIX
				shutdown(imp_->lisn_socket_, 2);
				close(imp_->lisn_socket_);
#endif
				break;
			case WORKING:
#ifdef WIN32
				shutdown(imp_->conn_socket_, 2);
				shutdown(imp_->lisn_socket_, 2);
				closesocket(imp_->conn_socket_);
				closesocket(imp_->lisn_socket_);
				WSACleanup();
#endif
#ifdef UNIX
				shutdown(imp_->conn_socket_, 2);
				shutdown(imp_->lisn_socket_, 2);
				close(imp_->conn_socket_);
				close(imp_->lisn_socket_);
#endif
				break;
			case WAITING_FOR_REPLY:
#ifdef WIN32
				shutdown(imp_->conn_socket_, 2);
				shutdown(imp_->lisn_socket_, 2);
				closesocket(imp_->conn_socket_);
				closesocket(imp_->lisn_socket_);
				WSACleanup();
#endif
#ifdef UNIX
				shutdown(imp_->conn_socket_, 2);
				shutdown(imp_->lisn_socket_, 2);
				close(imp_->conn_socket_);
				close(imp_->lisn_socket_);
#endif
				imp_->cv_reply_data_received_.notify_one();
				break;
			}
			
			if (std::this_thread::get_id() == imp_->recv_data_thread_.get_id())
			{
				imp_->recv_data_thread_.detach();
			}
			else if(imp_->recv_data_thread_.joinable())
			{
				imp_->recv_data_thread_.join();
			}
				
			if (std::this_thread::get_id() == imp_->recv_conn_thread_.get_id())
			{
				imp_->recv_conn_thread_.detach();
			}
			else if(imp_->recv_conn_thread_.joinable())
			{
				imp_->recv_conn_thread_.join();
			}

			imp_->state_ = Socket::IDLE;
		}
		auto Socket::isConnected()->bool
		{
			std::unique_lock<std::recursive_mutex> lck(imp_->state_mutex_);

			switch (imp_->state_)
			{
			case WORKING:
			case WAITING_FOR_REPLY:
				return true;
			default:
				return false;
			}
		}
		auto Socket::startServer(const char *port)->void
		{
			std::unique_lock<std::recursive_mutex> lck(imp_->state_mutex_);
			
			switch (imp_->state_)
			{
			case IDLE:
				break;
			default:
				throw(StartServerError( "Socket can't Start as server, because it is not at idle state\n",this,0 ));
			}

			// 启动服务器 //
#ifdef WIN32 
			if (WSAStartup(0x0101, &imp_->wsa_data_) != 0)
			{
				throw(StartServerError("Socket can't Start as server, because it can't WSAstartup\n", this, 0));
			}
#endif

			// 服务器端开始建立socket描述符 //
			if ((imp_->lisn_socket_ = socket(AF_INET, SOCK_STREAM, 0)) == -1)
			{
				throw(StartServerError("Socket can't Start as server, because it can't socket\n", this, 0));
			}

			// 服务器端填充server_addr_结构 //
			memset(&imp_->server_addr_, 0, sizeof(struct sockaddr_in));
			imp_->server_addr_.sin_family = AF_INET;
			imp_->server_addr_.sin_addr.s_addr = htonl(INADDR_ANY);
			imp_->server_addr_.sin_port = htons(atoi(port));

			// 捆绑lisn_socket_描述符 //
			if (::bind(imp_->lisn_socket_, (struct sockaddr *)(&imp_->server_addr_), sizeof(struct sockaddr)) == -1)
			{
#ifdef WIN32
				int err = WSAGetLastError();
#endif
				throw(StartServerError("Socket can't Start as server, because it can't bind\n", this, 0));
			}

			// 监听lisn_socket_描述符 //
			if (listen(imp_->lisn_socket_, 5) == -1)
			{
				throw(StartServerError("Socket can't Start as server, because it can't listen\n", this, 0));
			}

			// 启动等待连接的线程 //
			std::unique_lock<std::mutex> cv_lck(imp_->cv_mutex_);
			imp_->recv_conn_thread_ = std::thread(Socket::Imp::acceptThread, this->imp_.get());
			imp_->cv_.wait(cv_lck);
			
			return;
		}
		auto Socket::connect(const char *address, const char *port)->void
		{
			std::unique_lock<std::recursive_mutex> lck(imp_->state_mutex_);
			
			switch (imp_->state_)
			{
			case IDLE:
				break;
			default:
				throw ConnectError("Socket can't connect, because it is busy now, please close it\n", this, 0);
			}

			// 启动服务器 //
#ifdef WIN32
			if (WSAStartup(0x0101, &imp_->wsa_data_) != 0)
			{
				throw ConnectError("Socket can't connect, because can't WSAstartup\n", this, 0);
			}
#endif
			// 服务器端开始建立socket描述符 //
			if ((imp_->conn_socket_ = socket(AF_INET, SOCK_STREAM, 0)) < 0)
			{
				throw ConnectError("Socket can't connect, because can't socket\n", this, 0);
			}

			// 客户端填充server_addr_结构 //
			memset(&imp_->server_addr_, 0, sizeof(imp_->server_addr_));
			imp_->server_addr_.sin_family = AF_INET;
			imp_->server_addr_.sin_addr.s_addr = inet_addr(address); //与linux不同
			imp_->server_addr_.sin_port = htons(atoi(port));

			// 连接 //
			if (::connect(imp_->conn_socket_, (const struct sockaddr *)&imp_->server_addr_, sizeof(imp_->server_addr_)) == -1)
			{
				throw ConnectError("Socket can't connect, because can't connect\n", this, 0);
			}

			// Start Thread //
			imp_->recv_data_thread_ = std::thread(Imp::receiveThread, this->imp_.get());
			
			imp_->state_ = WORKING;

			return;
		}
		auto Socket::sendMsg(const aris::core::Msg &data)->void
		{
			std::unique_lock<std::recursive_mutex> lck(imp_->state_mutex_);

			switch (imp_->state_)
			{
			case WORKING:
			case WAITING_FOR_REPLY:
				if (send(imp_->conn_socket_, data.data_, data.size() + sizeof(MsgHeader), 0) == -1)
					throw SendDataError("Socket failed sending data, because network failed\n", this, 0);
				else
					return;
			default:
				throw SendDataError("Socket failed sending data, because Socket is not at right state\n", this, 0);
			}
		}
		auto Socket::sendRequest(const aris::core::Msg &request)->aris::core::Msg
		{
			std::unique_lock<std::recursive_mutex> state_lck(imp_->state_mutex_);

			switch (imp_->state_)
			{
			case WORKING:
				imp_->state_ = WAITING_FOR_REPLY;
				break;
			default:
				throw SendRequestError("Socket failed sending request, because Socket is not at right state\n", this, 0);
			}

			aris::core::Msg request_copy_ = request;
			request_copy_.setType(Socket::Imp::SOCKET_REQUEST);
			
			try
			{
				sendMsg(request_copy_);
			}
			catch (SendDataError &error)
			{
				throw SendRequestError(error.what(), this, 0);
			}
			

			imp_->cv_reply_data_received_.wait(state_lck);
			if (imp_->state_ != WAITING_FOR_REPLY)
			{
				throw SendRequestError("Socket failed sending request, because Socket is closed before it receive a reply\n", this, 0);
			}
			else
			{
				Msg reply;
				reply.swap(imp_->reply_msg_);
				return reply;
			}
		}
		auto Socket::setOnReceivedMsg(std::function<int(Socket*, aris::core::Msg &)> OnReceivedData)->void
		{
			std::unique_lock<std::recursive_mutex> lck(imp_->state_mutex_);
			imp_->onReceivedData = OnReceivedData;
		}
		auto Socket::setOnReceivedRequest(std::function<aris::core::Msg(Socket*, aris::core::Msg &)> OnReceivedRequest)->void
		{
			std::unique_lock<std::recursive_mutex> lck(imp_->state_mutex_);
			imp_->onReceivedRequest = OnReceivedRequest;
		}
		auto Socket::setOnReceivedConnection(std::function<int(Socket*, const char*, int)> OnReceivedConnection)->void
		{
			std::unique_lock<std::recursive_mutex> lck(imp_->state_mutex_);
			imp_->onReceivedConnection = OnReceivedConnection;
		}
		auto Socket::setOnLoseConnection(std::function<int(Socket*)> OnLoseConnection)->void
		{
			std::unique_lock<std::recursive_mutex> lck(imp_->state_mutex_);
			imp_->onLoseConnection = OnLoseConnection;
		}
		auto Socket::setOnAcceptError(std::function<void(Socket*)> onAcceptError)->void
		{
			std::unique_lock<std::recursive_mutex> lck(imp_->state_mutex_);
			imp_->onAcceptError = onAcceptError;
		}
		auto Socket::setOnReceiveError(std::function<void(Socket*)> onReceiveError)->void
		{
			std::unique_lock<std::recursive_mutex> lck(imp_->state_mutex_);
			imp_->onReceiveError = onReceiveError;
		}
	}
}


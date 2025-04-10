/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   User.cpp                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mpellegr <mpellegr@student.hive.fi>        +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/02/14 13:32:51 by pleander          #+#    #+#             */
/*   Updated: 2025/02/27 11:40:19 by jmakkone         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "User.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <stdexcept>
#include <string>

#include "Logger.hpp"
#include "Server.hpp"
#include "sys/socket.h"

const std::string User::avail_user_modes{"iwo"};
User::User() : sockfd_{-1}
{
}

User::User(int sockfd)
	: sockfd_{sockfd},
	  registered_{false},
	  isIrcOperator_{false},
	  awayMsg_{""},
	  usrChannelCount_{0}
{
	struct sockaddr_in addr;
	socklen_t addr_len = sizeof(addr);
	if (getpeername(sockfd_, (struct sockaddr*)&addr, &addr_len) == 0)
	{
		host_ = inet_ntoa(addr.sin_addr);
	}
	else
	{
		host_ = "unknown";
	}
}

/**
 * @brief Receives data from the user's socket. Throws an error on failure.
 *
 * @param buf: buffer to write data to
 * @return 1 if read successful, 0 if socket has been closed
 */
int User::receiveData()
{
	std::string buf(512, 0);
	int n_bytes = recv(this->sockfd_, const_cast<char*>(buf.data()), buf.size(),
					   MSG_DONTWAIT);
	// Client closed the connection
	if (n_bytes == 0)
	{
		close(this->sockfd_);
		return (0);
	}
	if (n_bytes < 0)
	{
		throw std::runtime_error{"Failed to receive data from user " + nick_};
	}
	buf = buf.substr(0, n_bytes);  // Truncate buf to size of data
	Logger::log(Logger::DEBUG, "Received data from user " +
								   std::to_string(sockfd_) + ": " + buf);
	recv_buf_ += buf;
	return (1);
}

/**
 * @brief Gets the next CRLF separated message from the recv stream and stores
 * it in buf. Returns 0 if the recv stream is empty
 *
 * @param buf buffer to store message in
 * @return 0 if recv stream is emtpy
 */
int User::getNextMessage(std::string& buf)
{
	if (this->sockfd_ < 0)
	{
		return (0);
	}
	std::string part;
	if (recv_buf_.size() == 0)
	{
		return (0);
	}
	size_t pos = recv_buf_.find("\r\n", 0);
	if (pos == std::string::npos)
	{
		return (0);
	}
	if (pos > 509)  // Message too long. 512 minus \r\n
	{
		sendData("ERROR message too long\r\n");
		recv_buf_.erase(recv_buf_.begin(), recv_buf_.begin() + pos + 2);
		return (0);
	}
	buf = recv_buf_.substr(0, pos);
	recv_buf_.erase(recv_buf_.begin(), recv_buf_.begin() + pos + 2);
	return (1);
}

/**
 * @brief Sends the data in the buffer to the user
 *
 * @param buf: data to send
 * @return bytes written or throw error
 */
int User::sendData(const std::string& buf)
{
	int n_bytes = send(this->sockfd_, buf.c_str(), buf.length(), MSG_DONTWAIT);
	if (n_bytes < 0)
	{
		Logger::log(Logger::WARNING,
					"Failed to send data to user " + nick_ + ": " + buf);
		return (n_bytes);
	}
	Logger::log(Logger::DEBUG, "Sent data to user " + nick_ + ": " + buf);
	return (n_bytes);
}

void User::setPassword(std::string& s)
{
	password_ = s;
}

void User::registerUser()
{
	Logger::log(Logger::DEBUG, "Registered user " + username_);
	registered_ = true;
}

bool User::isRegistered()
{
	return (registered_);
}

const std::string& User::getPassword() const
{
	return (password_);
}

void User::setNick(std::string& nick)
{
	nick_ = nick;
}

const std::string& User::getNick() const
{
	return (nick_);
}

void User::setUsername(std::string& username)
{
	username_ = username;
}

const std::string& User::getUsername() const
{
	return (username_);
}

void User::markUserForDeletion()
{
	sockfd_ = -1;
}
int User::getSocket() const
{
	return (sockfd_);
}

const std::string& User::getHost() const
{
	return (host_);
}

const std::string& User::getRealname() const
{
	return (realname_);
}

void User::setRealName(std::string& realname)
{
	realname_ = realname;
}

void User::setIsIrcOperator()
{
	isIrcOperator_ = true;
}

bool User::getIsIrcOperator()
{
	return (isIrcOperator_);
}

int User::getUsrChannelCount()
{
	return (usrChannelCount_);
}

void User::incUsrChannelCount()
{
	usrChannelCount_++;
}

void User::decUsrChannelCount()
{
	if (usrChannelCount_ > 0)
	{
		usrChannelCount_--;
	}
}

void User::setMode(char mode)
{
	if (modes_.find(mode) == std::string::npos)
	{
		modes_.push_back(mode);
	}
}

void User::unsetMode(char mode)
{
	size_t pos;
	while ((pos = modes_.find(mode)) != std::string::npos)
	{
		modes_.erase(pos, 1);
	}
}

bool User::hasMode(char mode) const
{
	return (modes_.find(mode) != std::string::npos);
}

std::string User::getModeString() const
{
	if (!modes_.empty())
	{
		return ("+" + modes_);
	}
	return "";
}

// Away is set by some other setter function, this heavily depend on OPER being
// implemented, so it's just a sketch with same format than applyChannelMode.
void User::applyUserMode(User& setter, const std::string& modes,
						 const std::string& param)
{
	// not sure do we need to bring parameters to this function, maybe we remove
	// this.
	(void)param;
	if (modes.empty())
	{
		setter.sendData(
			rplUmodeIs(SERVER_NAME, setter.getNick(), setter.getModeString()));
		return;
	}

	bool adding = true;
	for (size_t i = 0; i < modes.size(); ++i)
	{
		char c = modes[i];
		if (c == '+')
		{
			adding = true;
		}
		else if (c == '-')
		{
			adding = false;
		}
		else
		{
			if (User::avail_user_modes.find(c) == std::string::npos)
			{
				setter.sendData(
					errUnknownModeFlag(SERVER_NAME, setter.getNick()));
				return;
			}
			else if (c == 'i')
			{
				if (adding)
				{
					setMode('i');
				}
				else
				{
					unsetMode('i');
				}
			}
			else if (c == 'w')
			{
				if (adding)
				{
					setMode('w');
				}
				else
				{
					unsetMode('w');
				}
			}
			else if (c == 'o')
			{
				if (adding)
				{
					if (!setter.getIsIrcOperator())
					{
						setter.sendData(
							errNoPrivileges(SERVER_NAME, setter.getNick()));
						return;
					}
					setMode('o');
				}
				else
				{
					if (setter.getIsIrcOperator())
					{
						unsetMode('o');
						isIrcOperator_ = false;
					}
				}
			}
		}
	}

	std::string modeChangeMsg = ":" + setter.getNick() + "!" +
								setter.getUsername() + "@" + setter.getHost() +
								" MODE " + this->nick_ + " " + modes + "\r\n";
	setter.sendData(modeChangeMsg);
	if (this != &setter)
	{
		this->sendData(modeChangeMsg);
	}
}

void User::setAwayMsg(std::string msg)
{
	if (msg.empty())
		this->sendData(rplUnaway(SERVER_NAME, nick_));
	else
		this->sendData(rplNowAway(SERVER_NAME, nick_));
	awayMsg_ = msg;
}

std::string User::getAwayMsg() const
{
	return awayMsg_;
}

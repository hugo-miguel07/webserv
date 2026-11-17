#pragma once
#include "test.hpp"
#include "Request.hpp"
#include "Response.hpp"
#include "ServerConfig.hpp"
#include "Locations.hpp"

class Handler
{
	public:
		enum e_status
		{
			DONE,
			NEED_CGI,
			ERROR
		};

		Handler();
		virtual ~Handler();

		virtual e_status handle(const Request &req,
								Response &res,
								const ServerConfig &srv,
								const Locations &loc) = 0;

	protected:

		void        buildError(Response &res, int statusCode) const;

		e_status    notAllowed(Response &res, const Locations &loc) const;

		bool        resolvePath(const std::string &root,
								const std::string &requestPath,
								std::string &outPath) const;

		bool        isMethodAllowed(const Request &req,
									const Locations &loc) const;

		bool        isFile(const std::string &path) const;

		bool        isDirectory(const std::string &path) const;
};

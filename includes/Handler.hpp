#pragma once
#include "test.hpp"
#include "Request.hpp"
#include "Response.hpp"
#include "ServerConfig.hpp"

/*
	Abstract interface for all request handlers.
	Each handler receives the parsed request, fills the response, and
	returns a status so the Server can decide what to do next.
*/
class Handler
{
	public:
		enum e_status
		{
			DONE,       // response fully built, ready to send
			NEED_CGI,   // keep connection open, wait for CGI process
			ERROR       // internal failure, connection should be closed
		};

		Handler();
		virtual ~Handler();

		// Main entry point: process the request and fill the response.
		virtual e_status handle(const Request &req, Response &res) = 0;

	protected:
		// Shared helpers available to every concrete handler.

		// Build a minimal error response (status + default body).
		void        buildError(Response &res, int statusCode) const;

		// Resolve request path against root; false if traversal outside root.
		bool        resolvePath(const std::string &root,
								const std::string &requestPath,
								std::string &outPath) const;

		// True if method is present in the location's allowed methods.
		bool        isMethodAllowed(const Request &req,
									const Locations &loc) const;
};

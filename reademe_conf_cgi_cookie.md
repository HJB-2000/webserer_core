# Webserv README

## Configuration File

A configuration file (often called a *config file*) is a file that contains parameters or settings used to control the behavior of a software application. It is typically loaded at startup when the application is launched.

For our project, we took inspiration from the NGINX configuration model.

At first glance, you will notice that the configuration file is organized in a tree-like structure using `{}`. In NGINX terminology, these blocks are called *contexts* because they group configuration directives related to a specific scope or concern. This hierarchical structure allows configurations to be applied conditionally and inherited across nested contexts.

Directives can only be used in the contexts for which they are designed. Therefore, a configuration file is mainly composed of:

* **Contexts**
* **Directives**

In our Webserv implementation, based on the subject requirements, we support:

* `http` context
* `server` context
* `location` context

### HTTP Context

The `http` context contains the majority of the configuration. It defines global behavior related to HTTP handling and includes all server blocks.

### Server Context

The `server` context is declared inside the `http` context. Multiple server blocks can be defined, each representing a virtual server handling client requests.

Our server selects the appropriate `server` block based on:

* `listen` (IP/port)
* `server_name` (domain name)

### Location Context

The `location` context is defined inside a `server` block and is used to handle specific request URIs.

While the server block is selected using the IP/port combination, the `location` block refines request handling based on the URI (the part of the URL after the domain and port).

**Resource:**
https://www.digitalocean.com/community/tutorials/understanding-the-nginx-configuration-file-structure-and-configuration-contexts

---

## CGI (Common Gateway Interface)

In this project, our HTTP server goes beyond serving static content by implementing the CGI protocol to handle dynamic content.

CGI acts as a standardized bridge between our C++ web server and external programs (such as Python or shell scripts).

### Process Lifecycle: Forking per Request

For each incoming request that requires dynamic processing, the server uses a *process-per-request* model.

* **Forking (fork & execve):**
  When a request targets a CGI route, the server calls `fork()` to create a child process, then uses `execve()` to execute the script. This isolates execution from the main server loop.

* **Safe Execution:**
  Each CGI runs in its own process. If a script crashes or enters an infinite loop, it only affects the child process and not the main server.

* **Environment Variables:**
  Before execution, the server extracts request metadata (e.g., `REQUEST_METHOD`, `HEADERS`, `QUERY_STRING`) and passes them as environment variables to the CGI process.

### Note on NGINX

NGINX uses **FastCGI**, an optimized version of CGI that avoids creating a new process per request by maintaining a pool of persistent processes.

**Resource:**
https://www.cgi101.com/book/ch3/text.html

---

## Cookies

### The Problem: HTTP is Stateless

HTTP is a *stateless protocol*, meaning each request is independent. The server does not retain any memory of previous interactions.

Without a state mechanism:

* A user logs in successfully
* But on the next request, the server forgets the user
* The user would need to re-authenticate every time

This leads to a poor user experience.

### The Solution: Cookies

Cookies allow the server to maintain state across multiple HTTP requests.

#### Workflow

1. Client sends login request
2. Server validates and responds with `Set-Cookie`
3. Browser stores the cookie
4. Future requests automatically include the cookie

```
[Client] ---- POST /login ----> [Server]
[Client] <--- Set-Cookie: id --- [Server]
        (browser stores cookie)
[Client] ---- GET /home + Cookie ---> [Server]
```

### Session Handling

* The server creates a unique session ID
* Sends it via the `Set-Cookie` header
* The browser stores it automatically
* Each future request includes this ID

### Key Attributes

* **Expires / Max-Age:**
  Defines how long the cookie remains valid

* **HttpOnly:**
  Prevents JavaScript access to cookies, protecting against XSS attacks

**Resource:**
https://developer.mozilla.org/en-US/docs/Web/HTTP/Guides/Cookies

---

## Instructions (How to Run)

After compiling the project successfully, you can run the server using:

```
./webserv [path_to_configuration_file]
```

* If a configuration file is provided, the server will use it
* If not, the server will fall back to a default configuration path

Make sure your configuration file is valid before running the server.

---

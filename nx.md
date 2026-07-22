npx @modelcontextprotocol/inspector --url http://localhost:6543/mcp --transport http


### libdef file change

plat wnt:
    CXX_FLAGS   $CXX_FLAGS -ID:\workspace\github\cpp-mcp-sdk\dist\mcp_sdk\include
    libs        $libs mcp_sdk
.
---

Act as a Principal Software Architect. I want to build a custom, production-grade Model Context Protocol (MCP) server entirely from scratch using native HTTP and gRPC network libraries, avoiding standard pre-built MCP SDKs. 

The implementation must be highly modular, scalable, and secure, utilizing object-oriented/structured design patterns (such as classes, structs, interfaces, and dependency injection).

### 1. Architectural & Core Requirements
* **Language Preference:** [Specify your language here, e.g., Go, TypeScript, Python, or Rust]
* **Transport Layer:** Implement a hybrid/streamable approach supporting custom gRPC and/or HTTP streaming endpoints from scratch using core/standard libraries.
* **Configuration:** The server must accept a configurable port and initialization properties via an environment variable or configuration struct/class.
* **Authentication:** Implement a robust custom pluggable authentication mechanism (e.g., Bearer Token, API Key validation, or mTLS) as a middleware layer protecting the endpoints.

### 2. Modular Capability Design (The 4 Pillars)
The server architecture must strictly separate the following MCP capabilities into isolated modules using explicit classes/structs and clean interface boundaries:

* **Resources Module:** For exposing read-only data, file schemas, or application states to the client. Must include methods for discovery (`ListResources`) and retrieval (`ReadResource`).
* **Apps (Context/Lifecycle) Module:** For managing the active application scope, session handling, and tracking client state.
* **Tools Module:** For registering executable functions that the AI client can invoke. Must include discovery (`ListTools`) and execution (`CallTool`) mechanisms with strict parameter schemas.
* **Prompts & Templates Module:** For exposing predefined prompts or dynamic templates that the client can fetch and populate with variables (`GetPrompt`, `ListPrompts`).

### 3. Structural Design Patterns Requested
* **Registry Pattern:** Implement a central `MCPRegistry` class/struct where Resources, Tools, Prompts, and Apps can be dynamically registered, looked up, and injected into the transport handlers.
* **Request/Response Cycle:** Define explicit strongly-typed Structs/Classes for all incoming requests and outgoing responses, mapping out how the custom gRPC/HTTP payloads match the conceptual requirements of MCP (handling parameters, metadata, and data streaming).
* **Middleware Pipeline:** Show a clear example of how an incoming stream/request hits the Authentication middleware before resolving to the specific module registry.

### 4. Output Deliverables
1. A clear file tree showing the modular, decoupled directory structure.
2. The core abstract interfaces or base structs for the 4 modules (Resources, Apps, Tools, Prompts).
3. The `MCPRegistry` and Server bootstrap code implementing the custom port configuration and authentication middleware.
4. A minimal boilerplate example of an HTTP/gRPC stream endpoint handling a `CallTool` request from scratch.

Provide clean, heavily commented, production-ready code with no hand-waving or omissions for the architectural backbone.


### Dependency Analysis

D:\workdir\devunits\cmd-mcp-server>dumpbin /dependents mfgservices.exe
Microsoft (R) COFF/PE Dumper Version 14.42.34436.0
Copyright (C) Microsoft Corporation.  All rights reserved.


Dump of file mfgservices.exe

File Type: EXECUTABLE IMAGE

  Image has the following dependencies:

    libsyss.dll
    libmfgservicecommands.dll
    libufun.dll
    libufunx.dll
    libpart.dll
    libjson.dll
    libjam.dll
    libpoco.dll
    libnxmcp.dll
    MSVCP140.dll
    VCRUNTIME140.dll
    VCRUNTIME140_1.dll
    api-ms-win-crt-runtime-l1-1-0.dll
    api-ms-win-crt-heap-l1-1-0.dll
    api-ms-win-crt-string-l1-1-0.dll
    api-ms-win-crt-math-l1-1-0.dll
    api-ms-win-crt-convert-l1-1-0.dll
    api-ms-win-crt-environment-l1-1-0.dll
    api-ms-win-crt-stdio-l1-1-0.dll
    api-ms-win-crt-locale-l1-1-0.dll
    KERNEL32.dll
    WS2_32.dll
    USER32.dll

  Summary

        2000 .data
        4000 .pdata
        D000 .rdata
        1000 .reloc
        1000 .rsrc
       40000 .text

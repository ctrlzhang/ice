// **********************************************************************
//
// Copyright (c) 2003-2004 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

#include <IceUtil/UUID.h>
#include <Ice/Service.h>
#include <Glacier2/SessionRouterI.h>
#include <Glacier2/CryptPermissionsVerifierI.h>

using namespace std;
using namespace Ice;
using namespace Glacier2;

namespace Glacier2
{

class RouterService : public Service
{
public:

    RouterService();

protected:

    virtual bool start(int, char*[]);
    virtual bool stop();
    virtual CommunicatorPtr initializeCommunicator(int&, char*[]);

private:

    void usage(const std::string&);

    SessionRouterIPtr _sessionRouter;
};

};

Glacier2::RouterService::RouterService()
{
}

bool
Glacier2::RouterService::start(int argc, char* argv[])
{
    for(int i = 1; i < argc; ++i)
    {
	if(strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
	{
	    usage(argv[0]);
	    return EXIT_SUCCESS;
	}
	else if(strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0)
	{
	    cout << ICE_STRING_VERSION << endl;
	    return EXIT_SUCCESS;
	}
	else
	{
	    cerr << argv[0] << ": unknown option `" << argv[i] << "'" << endl;
	    usage(argv[0]);
	    return EXIT_FAILURE;
	}
    }

    PropertiesPtr properties = communicator()->getProperties();

    //
    // Initialize the client object adapter.
    //
    const char* clientEndpointsProperty = "Glacier2.Client.Endpoints";
    if(properties->getProperty(clientEndpointsProperty).empty())
    {
	cerr << argv[0] << ": property `" << clientEndpointsProperty << "' is not set" << endl;
	return EXIT_FAILURE;
    }
    ObjectAdapterPtr clientAdapter = communicator()->createObjectAdapter("Glacier2.Client");

    //
    // Initialize the server object adapter only if server endpoints
    // are defined.
    //
    const char* serverEndpointsProperty = "Glacier2.Server.Endpoints";
    ObjectAdapterPtr serverAdapter;
    if(!properties->getProperty(serverEndpointsProperty).empty())
    {
	serverAdapter = communicator()->createObjectAdapter("Glacier2.Server");
    }

    //
    // Get the permissions verifier, or create a default one if no
    // verifier is specified.
    //
    string verifierProperty = properties->getProperty("Glacier2.PermissionsVerifier");
    PermissionsVerifierPrx verifier;
    if(!verifierProperty.empty())
    {
	verifier = PermissionsVerifierPrx::checkedCast(communicator()->stringToProxy(verifierProperty));
	if(!verifier)
	{
	    error("permissions verifier `" + verifierProperty + "' is invalid");
	    return false;
	}
    }
    else
    {
	string passwordsProperty = properties->getPropertyWithDefault("Glacier2.CryptPasswords", "passwords");

	ifstream passwordFile(passwordsProperty.c_str());
	if(!passwordFile)
	{
            string err = strerror(errno);
	    error("cannot open `" + passwordsProperty + "' for reading: " + err);
	    return false;
	}

	map<string, string> passwords;

	while(true)
	{
	    string userId;
	    passwordFile >> userId;
	    if(!passwordFile)
	    {
		break;
	    }

	    string password;
	    passwordFile >> password;
	    if(!passwordFile)
	    {
		break;
	    }

	    assert(!userId.empty());
	    assert(!password.empty());
	    passwords.insert(make_pair(userId, password));
	}

	PermissionsVerifierPtr verifierImpl = new CryptPermissionsVerifierI(passwords);

	//
	// We need a separate object adapter for any collocated
	// permissions verifier. We can't use the client adapter.
	//
	ObjectAdapterPtr verifierAdapter =
	    communicator()->createObjectAdapterWithEndpoints(IceUtil::generateUUID(), "");
	verifier = PermissionsVerifierPrx::uncheckedCast(verifierAdapter->addWithUUID(verifierImpl));
    }

    //
    // Create a router implementation that can handle sessions, and
    // add it to the client object adapter.
    //
    _sessionRouter = new SessionRouterI(clientAdapter, serverAdapter);
    const char* routerIdProperty = "Glacier2.RouterIdentity";
    Identity routerId = stringToIdentity(properties->getPropertyWithDefault(routerIdProperty, "Glacier2/router"));
    clientAdapter->add(_sessionRouter, routerId);

    //
    // Everything ok, let's go.
    //
    clientAdapter->activate();

    return true;
}

bool
Glacier2::RouterService::stop()
{
    //
    // Destroy the session router.
    //
    assert(_sessionRouter);
    _sessionRouter->destroy();

    return true;
}

CommunicatorPtr
Glacier2::RouterService::initializeCommunicator(int& argc, char* argv[])
{
    try
    {
	PropertiesPtr defaultProperties = getDefaultProperties(argc, argv);

	//
	// Make sure that Glacier2 doesn't use a router.
	//
	defaultProperties->setProperty("Ice.Default.Router", "");

	//
	// No active connection management is permitted with
	// Glacier2. Connections must remain established.
	//
	defaultProperties->setProperty("Ice.ConnectionIdleTime", "0");

	//
	// Ice.MonitorConnections defaults to Ice.ConnectionIdleTime,
	// which we set to 0 above. However, we still want the
	// connection monitor thread for AMI timeouts. We only set
	// this value if it hasn't been set explicitly already.
	//
	if(defaultProperties->getProperty("Ice.MonitorConnections").empty())
	{
	    defaultProperties->setProperty("Ice.MonitorConnections", "60");
	}

	//
	// We do not need to set Ice.RetryIntervals to -1, i.e., we do
	// not have to disable connection retry. It is safe for
	// Glacier2 to retry outgoing connections to servers. Retry
	// for incoming connections from clients must be disabled in
	// the clients.
	//
    }
    catch(const Exception& e)
    {
	cerr << e << endl;
	exit(EXIT_FAILURE);
    }

    return Service::initializeCommunicator(argc, argv);
}

void
Glacier2::RouterService::usage(const string& appName)
{
    string options =
	"Options:\n"
	"-h, --help           Show this message.\n"
	"-v, --version        Display the Ice version.";
#ifdef _WIN32
    if(checkSystem())
    {
        options.append(
	"\n"
	"\n"
	"--service NAME       Run as the Windows service NAME.\n"
	"\n"
	"--install NAME [--display DISP] [--executable EXEC] [args]\n"
	"                     Install as Windows service NAME. If DISP is\n"
	"                     provided, use it as the display name,\n"
	"                     otherwise NAME is used. If EXEC is provided,\n"
	"                     use it as the service executable, otherwise\n"
	"                     this executable is used. Any additional\n"
	"                     arguments are passed unchanged to the\n"
	"                     service at startup.\n"
	"--uninstall NAME     Uninstall Windows service NAME.\n"
	"--start NAME [args]  Start Windows service NAME. Any additional\n"
	"                     arguments are passed unchanged to the\n"
	"                     service.\n"
	"--stop NAME          Stop Windows service NAME."
        );
    }
#else
    options.append(
        "\n"
        "\n"
        "--daemon             Run as a daemon.\n"
        "--noclose            Do not close open file descriptors.\n"
        "--nochdir            Do not change the current working directory."
    );
#endif
    cerr << "Usage: " << appName << " [options]" << endl;
    cerr << options << endl;
}

int
main(int argc, char* argv[])
{
    Glacier2::RouterService svc;
    return svc.main(argc, argv);
}

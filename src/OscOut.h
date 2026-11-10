#pragma once

namespace midiroll
{
	// Minimal OSC sender for the Resolume dashboard addresses.
	//
	// Sends float messages to 127.0.0.1:<port>. The socket is non-blocking so a
	// slow or missing receiver can never stall ProcessOpenGL.
	class OscOut
	{
	public:
		OscOut();
		~OscOut();

		bool Open( int port );
		void Close();
		bool IsOpen() const;

		// address must be a fixed OSC path such as /composition/dashboard/link1.
		void SendFloat( const char* address, float value );

	private:
		unsigned long long socket_  = ~0ull;
		unsigned int targetIp_      = 0;
		unsigned short targetPort_  = 0;
		bool winsockReady_          = false;
	};
}

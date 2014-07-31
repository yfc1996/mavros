/**
 * Test mavconn library
 */

// Gmock broken on Ubuntu (thrusty),
//  its gmock 1.6 require gtest 1.7, while repository only provides 1.6
//  this error exist one year without any updates.
//  https://bugs.launchpad.net/ubuntu/+source/google-mock/+bug/1201279
//
//  I think on Debian that was fixed while ago. But i can't use it in ros buildfarm.
//#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <ros/ros.h>

#include <mavros/mavconn_interface.h>
#include <mavros/mavconn_serial.h>
#include <mavros/mavconn_udp.h>
#include <mavros/mavconn_tcp.h>

using namespace mavconn;

TEST(MAVConn, allocate_check)
{
	std::unique_ptr<MAVConnInterface> conns[3];

	conns[0].reset(new MAVConnUDP(42, 200, "localhost", 45000));
	conns[1].reset(new MAVConnUDP(42, 201, "localhost", 45001));
	conns[2].reset(new MAVConnUDP(42, 202, "localhost", 45002));

	conns[1].reset(); // delete before allocation to ensure index
	conns[1].reset(new MAVConnUDP(42, 203, "localhost", 45003));
	ASSERT_EQ(conns[1]->get_channel(), 1);
}

#if 0
class MockMessageHandler {
public:
	MOCK_METHOD3(recv_message, void(const mavlink_message_t *msg, uint8_t sysid, uint8_t compid));
}
#endif

// This tests broken by mavconn-libev version. Issue #72.
class MessageHandler {
private:
	boost::condition_variable cond_var;
	ros::Duration wait_dt;

public:
	uint8_t message_id;

	MessageHandler() :
		message_id(255),
		wait_dt(10.0)
	{ }

	void recv_message(const mavlink_message_t *message, uint8_t sysid, uint8_t compid) {
		message_id = message->msgid;
		cond_var.notify_all();
	}

	bool wait_one(void) {
		boost::mutex cond_mutex;
		boost::unique_lock<boost::mutex> lock(cond_mutex);
		return cond_var.timed_wait(lock, wait_dt.toBoost());
	}

	static void send_heartbeat(MAVConnInterface *ip) {
		mavlink_message_t msg;
		mavlink_msg_heartbeat_pack_chan(ip->get_system_id(), ip->get_component_id(), ip->get_channel(), &msg,
				MAV_TYPE_ONBOARD_CONTROLLER,
				MAV_AUTOPILOT_INVALID,
				MAV_MODE_MANUAL_ARMED,
				0,
				MAV_STATE_ACTIVE);
		ip->send_message(&msg);
	}
};

TEST(UDP, bind_error)
{
	std::unique_ptr<MAVConnInterface> conns[2];

	conns[0].reset(new MAVConnUDP(42, 200, "localhost", 45000));
	ASSERT_THROW(conns[1].reset(new MAVConnUDP(42, 200, "localhost", 45000)), DeviceError);
}

TEST(UDP, send_message)
{
	// Issue #72
	std::unique_ptr<MAVConnInterface> echo;
	std::unique_ptr<MAVConnInterface> client;

	// create echo server
	echo.reset(new MAVConnUDP(42, 200, "0.0.0.0", 45000));
	echo->message_received.connect(boost::bind(&MAVConnInterface::send_message, echo.get(), _1, _2, _3));

	usleep(100000);

	// create client
	MessageHandler handler;
	client.reset(new MAVConnUDP(44, 200, "0.0.0.0", 45001, "localhost", 45000));
	client->message_received.connect(boost::bind(&MessageHandler::recv_message, &handler, _1, _2, _3));

	// wait echo
	MessageHandler::send_heartbeat(client.get());
	MessageHandler::send_heartbeat(client.get());
	EXPECT_EQ(handler.wait_one(), true);
	EXPECT_EQ(handler.message_id, MAVLINK_MSG_ID_HEARTBEAT);
}

TEST(TCP, bind_error)
{
	std::unique_ptr<MAVConnInterface> conns[2];

	conns[0].reset(new MAVConnTCPServer(42, 200, "localhost", 57600));
	ASSERT_THROW(conns[1].reset(new MAVConnTCPServer(42, 200, "localhost", 57600)), DeviceError);
}

TEST(TCP, connect_error)
{
	std::unique_ptr<MAVConnInterface> client;
	ASSERT_THROW(client.reset(new MAVConnTCPClient(42, 200, "localhost", 57666)), DeviceError);
}

TEST(TCP, send_message)
{
	// Issue #72
	std::unique_ptr<MAVConnInterface> echo_server;
	std::unique_ptr<MAVConnInterface> client;

	// create echo server
	echo_server.reset(new MAVConnTCPServer(42, 200, "0.0.0.0", 57600));
	echo_server->message_received.connect(boost::bind(&MAVConnInterface::send_message, echo_server.get(), _1, _2, _3));

	usleep(100000);

	// create client
	MessageHandler handler;
	client.reset(new MAVConnTCPClient(44, 200, "localhost", 57600));
	client->message_received.connect(boost::bind(&MessageHandler::recv_message, &handler, _1, _2, _3));

	// wait echo
	MessageHandler::send_heartbeat(client.get());
	MessageHandler::send_heartbeat(client.get());
	EXPECT_EQ(handler.wait_one(), true);
	ASSERT_EQ(handler.message_id, MAVLINK_MSG_ID_HEARTBEAT);
}

TEST(SERIAL, open_error)
{
	std::unique_ptr<MAVConnInterface> serial;
	ASSERT_THROW(serial.reset(new MAVConnSerial(42, 200, "/some/magic/not/exist/path", 57600)), DeviceError);
}

TEST(URL, open_url)
{
	boost::shared_ptr<MAVConnInterface>
		serial,
		udp,
		tcp_server,
		tcp_client;

	MAVConnSerial *serial_p;
	MAVConnUDP *udp_p;
	MAVConnTCPServer *tcp_server_p;
	MAVConnTCPClient *tcp_client_p;

#if 0
	/* not best way to test tty access,
	 * but it does not require any preparation
	 * Disabled because it breaks terminal.
	 */
	EXPECT_NO_THROW({
		serial = MAVConnInterface::open_url("/dev/tty:115200");
		serial_p = dynamic_cast<MAVConnSerial*>(serial.get());
		EXPECT_NE(serial_p, nullptr);
	});

	EXPECT_NO_THROW({
		serial = MAVConnInterface::open_url("serial:///dev/tty:115200?ids=2,240");
		serial_p = dynamic_cast<MAVConnSerial*>(serial.get());
		EXPECT_NE(serial_p, nullptr);
	});
#endif

	EXPECT_NO_THROW({
		udp = MAVConnInterface::open_url("udp://localhost:45000@localhost:45005/?ids=2,241");
		udp_p = dynamic_cast<MAVConnUDP*>(udp.get());
		tcp_server_p = dynamic_cast<MAVConnTCPServer*>(udp.get());
		EXPECT_NE(udp_p, nullptr);
		EXPECT_EQ(tcp_server_p, nullptr);
	});

	EXPECT_NO_THROW({
		udp = MAVConnInterface::open_url("udp://@localhost:45005");
		udp_p = dynamic_cast<MAVConnUDP*>(udp.get());
		EXPECT_NE(udp_p, nullptr);
	});

	EXPECT_NO_THROW({
		udp = MAVConnInterface::open_url("udp://localhost:45000@");
		udp_p = dynamic_cast<MAVConnUDP*>(udp.get());
		EXPECT_NE(udp_p, nullptr);
	});

	EXPECT_THROW({
		udp = MAVConnInterface::open_url("udp://localhost:45000");
	}, DeviceError);

	EXPECT_NO_THROW({
		tcp_server = MAVConnInterface::open_url("tcp-l://localhost:57600");
		tcp_server_p = dynamic_cast<MAVConnTCPServer*>(tcp_server.get());
		EXPECT_NE(tcp_server_p, nullptr);
	});

	EXPECT_NO_THROW({
		tcp_client = MAVConnInterface::open_url("tcp://localhost:57600");
		tcp_client_p = dynamic_cast<MAVConnTCPClient*>(tcp_client.get());
		EXPECT_NE(tcp_client_p, nullptr);
	});
}

int main(int argc, char **argv){
	ros::init(argc, argv, "mavconn_test", ros::init_options::AnonymousName);
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}

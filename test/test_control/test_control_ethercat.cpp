﻿#include <iostream>
#include <aris_control.h>
#include "test_control_ethercat.h"

const char xml_file[] =
"<?xml version=\"1.0\" encoding=\"UTF-8\" ?>"
"<controller>"
"	<data_logger type=\"DataLogger\"/>"
"	<slave_type_pool type=\"SlaveTypePoolObject\"/>"
"	<slave_pool type=\"SlavePoolObject\">"
"		<sla type=\"EthercatSlave\" phy_id=\"0\" product_code=\"0x00030924\" vendor_id=\"0x0000009a\" revision_num=\"0x000103F6\" dc_assign_activate=\"0x0300\" min_pos=\"0.676\" max_pos=\"1.091\" max_vel=\"0.2362\" home_pos=\"0.676\" input2count=\"22937600\">"
"			<pdo_group_pool type=\"PdoGroupPoolObject\">"
"				<index_1a00 type=\"PdoGroup\" default_child_type=\"Pdo\" index=\"0x1A00\" is_tx=\"true\">"
"					<pos_actual_value index=\"0x6064\" subindex=\"0x00\" size=\"4\"/>"
"				</index_1a00>"
"			</pdo_group_pool>"
"			<sdo_pool type=\"SdoPoolObject\" default_child_type=\"Sdo\">"
"				<home_mode index=\"0x6098\" subindex=\"0\" size=\"1\" config=\"17\" read=\"true\" write=\"true\"/>"
"			</sdo_pool>"
"		</sla>"
"	</slave_pool>"
"</controller>";

using namespace aris::control;

void test_pdo_xml()
{
	try
	{
		std::cout << "test pdo xml" << std::endl;
		
		aris::control::EthercatMaster m;

		auto &s1 = m.slavePool().add<EthercatSlave>();
		s1.loadXmlStr(
			"<sla type=\"EthercatSlave\" phy_id=\"0\" product_code=\"0x00030924\" vendor_id=\"0x0000009a\" revision_num=\"0x000103F6\" dc_assign_activate=\"0x0300\" min_pos=\"0.676\" max_pos=\"1.091\" max_vel=\"0.2362\" home_pos=\"0.676\" input2count=\"22937600\">"
			"	<pdo_group_pool type=\"PdoGroupPoolObject\">"
			"		<index_1a00 type=\"PdoGroup\" default_child_type=\"Pdo\" index=\"0x1A00\" is_tx=\"true\">"
			"			<pos_actual_value index=\"0x6064\" subindex=\"0x00\" size=\"4\"/>"
			"		</index_1a00>"
			"	</pdo_group_pool>"
			"</sla>");

		m.setControlStrategy([&]()
		{
			static int count{ 0 };

			std::int32_t value{ 0 };
			s1.readPdo(0x6064, 0x00, value);

			if (++count % 1000 == 0)
			{
				m.mout() << "count " << count << " : pos " << value << '\0';
				m.mout().update();
				m.sendOut();
			}
		});
		m.start();
		for (auto i{ 0 }; i < 20; ++i)
		{
			aris::core::Msg msg;
			while (!m.recvOut(msg));
			std::cout << msg.data() << std::endl;
		}
		m.stop();
		std::cout << "test pdo xml finished" << std::endl;
	}
	catch (std::exception &e)
	{
		std::cout << e.what() << std::endl;
	}
}
void test_pdo_code() 
{
	try 
	{
		aris::control::EthercatMaster m;

		auto &s1 = m.slavePool().add<EthercatSlave>("s1", 0, 0x0000009a, 0x00030924, 0x000103F6, 0x0300);
		auto &pdo_group = s1.pdoGroupPool().add<PdoGroup>("index_1a00", 0x1A00, true);
		pdo_group.add<Pdo>("index_6064", 0x6064, 0x00, 4);

		m.setControlStrategy([&]()
		{
			static int count{ 0 };

			std::int32_t value{ 0 };
			s1.readPdo(0x6064, 0x00, value);

			if (++count % 1000 == 0)
			{
				m.mout() << "count " << count << " : pos " << value << '\0';
				m.mout().update();
				m.sendOut();
			}
		});
		m.start();
		for (auto i{ 0 }; i < 20; ++i)
		{
			aris::core::Msg msg;
			while (!m.recvOut(msg));
			std::cout << msg.data() << std::endl;
		}
		m.stop();
		std::cout << "test pdo code finished" << std::endl;
	}
	catch (std::exception &e)
	{
		std::cout << e.what() << std::endl;
	}
}
void test_sdo_xml()
{
	try
	{
		std::cout << "test sdo code" << std::endl;

		aris::control::EthercatMaster m;

		auto &s1 = m.slavePool().add<EthercatSlave>();
		s1.loadXmlStr(
			"<sla type=\"EthercatSlave\" phy_id=\"0\" product_code=\"0x00030924\" vendor_id=\"0x0000009a\" revision_num=\"0x000103F6\" dc_assign_activate=\"0x0300\" min_pos=\"0.676\" max_pos=\"1.091\" max_vel=\"0.2362\" home_pos=\"0.676\" input2count=\"22937600\">"
			"	<sdo_pool type=\"SdoPoolObject\" default_child_type=\"Sdo\">"
			"		<home_mode index=\"0x6098\" subindex=\"0\" size=\"1\" config=\"17\" read=\"true\" write=\"true\"/>"
			"	</sdo_pool>"
			"</sla>");

		m.start();

		// test read sdo //
		std::int8_t mode = 0;
		s1.readSdo(0x6098, 0x00, mode);//home_mode.read(mode);
		std::cout << "home mode before write and start:" << static_cast<int>(mode) << std::endl;
		// test write sdo //
		s1.writeSdo(0x6098, 0x00, static_cast<std::int8_t>(16));//home_mode.write(static_cast<std::int8_t>(16));
		std::this_thread::sleep_for(std::chrono::seconds(3));
		s1.readSdo(0x6098, 0x00, mode);//home_mode.read(mode);
		std::cout << "home mode after write before start:" << static_cast<int>(mode) << std::endl;
		m.stop();


		// test config sdo //
		m.start();
		std::this_thread::sleep_for(std::chrono::seconds(3));
		s1.readSdo(0x6098, 0x00, mode);//home_mode.read(mode);
		std::cout << "home mode after start and config:" << static_cast<int>(mode) << std::endl;
		// test write sdo when running //
		s1.writeSdo(0x6098, 0x00, static_cast<std::int8_t>(17));//home_mode.write(static_cast<std::int8_t>(16));
		std::this_thread::sleep_for(std::chrono::seconds(3));
		s1.readSdo(0x6098, 0x00, mode);//home_mode.read(mode);
		std::cout << "home mode after write before start:" << static_cast<int>(mode) << std::endl;
		m.stop();
		std::cout << "test sdo code finished" << std::endl;
	}
	catch (std::exception &e)
	{
		std::cout << e.what() << std::endl;
	}


}
void test_sdo_code() 
{
	try
	{
		std::cout << "test sdo code" << std::endl;
		aris::control::EthercatMaster m;

		auto &s1 = m.slavePool().add<EthercatSlave>("s1", 0, 0x0000009a, 0x00030924, 0x000103F6, 0x0300);
		auto &home_mode = s1.sdoPool().add<Sdo>("index_6098", 0x6098, 0x00, sizeof(std::int16_t), Sdo::READ | Sdo::WRITE | Sdo::CONFIG, 17);

		m.start();

		// test read sdo //
		std::int8_t mode = 0;
		s1.readSdo(0x6098, 0x00, mode);//home_mode.read(mode);
		std::cout << "home mode before write and start:" << static_cast<int>(mode) << std::endl;
		// test write sdo //
		s1.writeSdo(0x6098, 0x00, static_cast<std::int8_t>(16));//home_mode.write(static_cast<std::int8_t>(16));
		std::this_thread::sleep_for(std::chrono::seconds(3));
		s1.readSdo(0x6098, 0x00, mode);//home_mode.read(mode);
		std::cout << "home mode after write before start:" << static_cast<int>(mode) << std::endl;
		m.stop();
		
		
		// test config sdo //
		m.start();
		std::this_thread::sleep_for(std::chrono::seconds(3));
		s1.readSdo(0x6098, 0x00, mode);//home_mode.read(mode);
		std::cout << "home mode after start and config:" << static_cast<int>(mode) << std::endl;
		// test write sdo when running //
		s1.writeSdo(0x6098, 0x00, static_cast<std::int8_t>(17));//home_mode.write(static_cast<std::int8_t>(16));
		std::this_thread::sleep_for(std::chrono::seconds(3));
		s1.readSdo(0x6098, 0x00, mode);//home_mode.read(mode);
		std::cout << "home mode after write before start:" << static_cast<int>(mode) << std::endl;
		m.stop();
		std::cout << "test sdo code finished" << std::endl;
	}
	catch (std::exception &e)
	{
		std::cout << e.what() << std::endl;
	}
	

}
void test_data_logger()
{
	try 
	{
		aris::core::XmlDocument xml_doc;
		std::cout<<"ret:"<<xml_doc.Parse(xml_file)<<std::endl;

		aris::control::EthercatMaster m;
		m.loadXmlDoc(xml_doc);

		m.setControlStrategy([&]()
		{
			static aris::core::MsgFix<8192> msg;
			static int count{ 0 };

			std::int32_t value{ 0 };
			m.ecSlavePool().front().readPdo(0x6064, 0x00, value);

			if (++count % 1000 == 0)
			{
				m.mout() << "count " << count << " : pos " << value << '\0';
				m.mout().update();
				m.sendOut();
			}

			m.dataLogger().lout() << count << " pos:" << value << "\n";
			m.dataLogger().send();
		});
		m.dataLogger().start();
		m.start();
		for (auto i{ 0 }; i < 20; ++i)
		{
			aris::core::Msg msg;
			while (!m.recvOut(msg));
			std::cout << msg.data() << std::endl;
		}
		m.stop();
		m.dataLogger().stop();
		std::cout << "test data logger finished" << std::endl;
	}
	catch (std::exception &e)
	{
		std::cout << e.what() << std::endl;
	}
}

void test_control_ethercat()
{
	test_pdo_code();
	test_pdo_xml();
	//test_sdo_code();
	//test_sdo_xml();
	test_data_logger();
}

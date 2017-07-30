﻿#ifndef ARIS_CONTROL_MASTER_SLAVE_H
#define ARIS_CONTROL_MASTER_SLAVE_H

#include <functional>
#include <thread>
#include <atomic>

#include <aris_core.h>
#include <aris_control_rt_timer.h>

namespace aris
{
	namespace control
	{
		class RTTimer : public aris::core::Object
		{
		public:
			static auto Type()->const std::string &{ static const std::string type("RTTimer"); return std::ref(type); }
			auto virtual type() const->const std::string&{ return Type(); }
			auto virtual saveXml(aris::core::XmlElement &xml_ele) const->void override;

			virtual ~RTTimer();
			RTTimer(const std::string &name);
			RTTimer(Object &father, const aris::core::XmlElement &xml_ele);
			RTTimer(const RTTimer &) = delete;
			RTTimer(RTTimer &&) = delete;
			RTTimer& operator=(const RTTimer &) = delete;
			RTTimer& operator=(RTTimer &&) = delete;

		private:
			struct Imp;
			aris::core::ImpPtr<Imp> imp_;
		};
		class DataLogger :public aris::core::Object
		{
		public:
			enum { MAX_LOG_DATA_SIZE = 8192 };
			static auto Type()->const std::string &{ static const std::string type("DataLogger"); return std::ref(type); }
			auto virtual type() const->const std::string&{ return Type(); }
			auto virtual saveXml(aris::core::XmlElement &xml_ele) const->void override;
			auto start(const std::string &log_file_name = std::string())->void;
			auto stop()->void;
			auto lout()->aris::core::MsgStream &;
			auto lout()const->const aris::core::MsgStream &{ return const_cast<DataLogger*>(this)->lout(); };
			auto send()->void;

			virtual ~DataLogger();
			DataLogger(const std::string &name);
			DataLogger(Object &father, const aris::core::XmlElement &xml_ele);
			DataLogger(const DataLogger &) = delete;
			DataLogger(DataLogger &&) = delete;
			DataLogger& operator=(const DataLogger &) = delete;
			DataLogger& operator=(DataLogger &&) = delete;

		private:
			struct Imp;
			aris::core::ImpPtr<Imp> imp_;
		};
		class SlaveType :public aris::core::Object
		{
		public:
			static auto Type()->const std::string &{ static const std::string type("SlaveType"); return std::ref(type); }
			auto virtual type() const->const std::string&{ return Type(); }

			virtual ~SlaveType() = default;
			explicit SlaveType(const std::string &name) :Object(name) {};
			explicit SlaveType(Object &father, const aris::core::XmlElement &xml_ele):Object(father, xml_ele) {};
			SlaveType(const SlaveType &) = default;
			SlaveType(SlaveType &&) = default;
			SlaveType& operator=(const SlaveType &) = default;
			SlaveType& operator=(SlaveType &&) = default;
		};
		class Slave : public aris::core::Object
		{
		public:
			static auto Type()->const std::string &{ static const std::string type("Slave"); return std::ref(type); }
			auto virtual type() const->const std::string&{ return Type(); }
			auto virtual send()->void {}
			auto virtual recv()->void {}
			auto slaveType()const->const SlaveType &{ return *slave_type_; }

			virtual ~Slave();
			explicit Slave(const std::string &name, const SlaveType &slave_type);
			explicit Slave(Object &father, const aris::core::XmlElement &xml_ele);
			Slave(const Slave &other);
			Slave(Slave &&other);
			Slave& operator=(const Slave &other);
			Slave& operator=(Slave &&other);

		private:
			const SlaveType *slave_type_;
		};
		class Master : public aris::core::Root
		{
		public:
			enum { MAX_MSG_SIZE = 8192 };
			using Root::loadXml;
			auto virtual loadXml(const aris::core::XmlDocument &xml_doc)->void override;
			auto virtual loadXml(const aris::core::XmlElement &xml_ele)->void override;
			auto start()->void;
			auto stop()->void;
			auto setControlStrategy(std::function<void()> strategy)->void;
			auto rtHandle()->Handle*;
			auto msgIn()->aris::core::MsgFix<MAX_MSG_SIZE>&;
			auto msgOut()->aris::core::MsgFix<MAX_MSG_SIZE>&;
			auto mout()->aris::core::MsgStream &;
			auto sendOut()->void;
			auto recvOut(aris::core::MsgBase &recv_msg)->int;
			auto sendIn(const aris::core::MsgBase &send_msg)->void;
			auto recvIn()->int;
			auto slaveTypePool()->aris::core::ObjectPool<SlaveType>&;
			auto slavePool()->aris::core::ObjectPool<Slave, Object>&;
			auto dataLogger()->DataLogger&;
			virtual ~Master();
			Master();
			Master(const Master &other) = delete;
			Master(Master &&other) = delete;
			Master& operator=(const Master &other) = delete;
			Master& operator=(Master &&other) = delete;

		protected:
			auto virtual init()->void {}
			auto virtual send()->void { for (auto &s : slavePool())s.send(); }
			auto virtual recv()->void { for (auto &s : slavePool())s.recv(); }
			auto virtual sync()->void {}
			auto virtual release()->void {}

		private:
			struct Imp;
			aris::core::ImpPtr<Imp> imp_;

			friend class Slave;
			friend class Sdo;
			friend class Pdo;
		};
	}
}

#endif

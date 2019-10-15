#include "../proxy/redis_protocol.h"
#include "base/logging.h"

#include <cassert>
#include <iostream>

void BulkArrayTest() {
  using namespace yarmproxy;
  {
    char data[] = "*1\r\n$-1\r\n";
    redis::BulkArray bulkv(data, sizeof(data) - 1);
    std::cout << "parsed_size\t:" << bulkv.parsed_size() << std::endl;
    return;

    assert(bulkv.parsed_size() == sizeof(data) - 1);
    assert(bulkv.total_size() == sizeof(data) - 1);

    assert(bulkv.total_bulks() == 1);
    assert(bulkv.present_bulks() == 1);
  }

  {
    char data[] = "*5\r\n$3\r\nset\r\n$4\r\nkey1\r\n$10\r\n00_abcdef_\r\n$2\r\nEX\r\n$5\r\n86400\r\n";
    redis::BulkArray bulkv(data, sizeof(data) - 1);
    assert(bulkv.parsed_size() == sizeof(data) - 1);
    assert(bulkv.total_size() == sizeof(data) - 1);
    assert(bulkv.total_bulks() == 5);
    assert(bulkv.present_bulks() == 5);
  }

  {
    char data[] = "*0\r\n"; // [] -> "*0\r\n"
    redis::BulkArray bulkv(data, sizeof(data) - 1);

    assert(bulkv.parsed_size() == 4);
    assert(bulkv.total_size() == 4);
    assert(bulkv.total_bulks() == 0);
    assert(bulkv.present_bulks() == 0);
  }

  {
    char data[] = "*2\r\n$3\r\nget\r\n$4\r\nkey7\r\n"; // [get,key7]
    redis::BulkArray bulkv(data, sizeof(data) - 1);
    // std::cout << "======= total_size\t:" << bulkv.total_size() << std::endl;
    // return;

    assert(bulkv.total_size() == 23);
    assert(bulkv.total_bulks() == 2);
    assert(bulkv.present_bulks() == 2);
    assert(bulkv.completed());
  }

  {
    // ["foo", "bar"] -> "*2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n"
    char data[] = "*2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n";
    redis::BulkArray bulkv(data, sizeof(data) - 1);
    std::cout << "total_size\t:" << bulkv.total_size() << std::endl;
    std::cout << "total_bulks\t:" << bulkv.total_bulks() << std::endl;
    std::cout << "present_bulks\t:" << bulkv.present_bulks() << std::endl;

    redis::Bulk& bulk = bulkv[1];
    std::cout << "\tpayload_size\t:" << bulk.payload_size() << std::endl;
    std::cout << "\ttotal_size\t:" << bulk.total_size() << std::endl;
    std::cout << "\tabsent_size\t:" << bulk.absent_size() << std::endl;
    std::cout << "\tto_string\t:" << bulk.to_string() << std::endl;

    assert(bulkv[0].equals("foo", sizeof("foo") - 1));
    assert(bulkv[1].equals("bar", sizeof("foo") - 1));
  }

  {
    char data[] = "*2\r";
    redis::BulkArray bulkv(data, sizeof(data) - 1);
    assert(bulkv.parsed_size() == 0);
  }

  {
    char data[] = "+2\r";
    redis::BulkArray bulkv(data, sizeof(data) - 1);
    assert(bulkv.parsed_size() == 0);
  }

  {
    char data[] = "*12\r\n";
    redis::BulkArray bulkv(data, sizeof(data) - 1);
    assert(bulkv.parsed_size() == 5);
    assert(bulkv.total_bulks() == 12);
    assert(bulkv.present_bulks() == 0);
  }

  {
    char data[] = "*12\r\n$3\r";
    redis::BulkArray bulkv(data, sizeof(data) - 1);
    assert(bulkv.parsed_size() == 5);
    assert(bulkv.total_bulks() == 12);
    assert(bulkv.present_bulks() == 0);
  }

  {
    char data[] = "*12\r\n$3\r\n";
    redis::BulkArray bulkv(data, sizeof(data) - 1);
    assert(bulkv.parsed_size() == 14);
    assert(bulkv.total_bulks() == 12);
    assert(bulkv.present_bulks() == 1);
  }

  {
    char data[] = "*12\r\n$3\r\nf";
    redis::BulkArray bulkv(data, sizeof(data) - 1);
    assert(bulkv.parsed_size() == 14);
    assert(bulkv.total_bulks() == 12);
    assert(bulkv.present_bulks() == 1);
    assert(bulkv[0].absent_size() == 4);
  }

  {
    char data[] = "*12\r\n$3\r";
    redis::BulkArray bulkv(data, sizeof(data) - 1);
    assert(bulkv.parsed_size() == 5);
    assert(bulkv.total_bulks() == 12);
    assert(bulkv.present_bulks() == 0);
  }

  {
    char data[] = "*12\r\n$3\r\nfoo";
    redis::BulkArray bulkv(data, sizeof(data) - 1);
    assert(bulkv.parsed_size() == 14);
    assert(bulkv.total_bulks() == 12);
    assert(bulkv.present_bulks() == 1);
    assert(bulkv[0].absent_size() == 2);
  }
}

void BulkTest() {
  using namespace yarmproxy;
  {
    char data[] = "$-1\r\n";
    redis::Bulk bulk(data, sizeof(data) - 1);
    assert(bulk.present_size() == 5);
    assert(bulk.payload_size() == 0);
    assert(bulk.total_size() == 5);
    assert(bulk.completed());
  }

  {
    char data[] = "+6\r";
    redis::Bulk bulk(data, sizeof(data) - 1);
    assert(bulk.present_size() == 0);
  }

  {
    char data[] = "+16\r\nabcdefg1234";
    redis::Bulk bulk(data, sizeof(data) - 1);
    assert(bulk.present_size() == redis::SIZE_PARSE_ERROR);
  }

  {
    char data[] = "$6\r";
    redis::Bulk bulk(data, sizeof(data) - 1);
    assert(bulk.present_size() == 0);
  }

  {
    char data[] = "$2\r\nhk\r\n$5\r\n";
    redis::Bulk bulk(data, sizeof(data) - 1);
    assert(bulk.present_size() == 8);
  }

  {
    char data[] = "$6\r\n";
    redis::Bulk bulk(data, 4);
    assert(bulk.present_size() == sizeof(data) - 1);
  }

  {
    char data[] = "$6\r\nre";
    redis::Bulk bulk(data, sizeof(data) - 1);
    assert(bulk.present_size() == sizeof(data) - 1);
  }

  {
    char data[] = "$8876\r\nre";
    redis::Bulk bulk(data, sizeof(data) - 1);
    assert(bulk.present_size() == sizeof(data) - 1);
  }

  {
    char data[] = "$6\r\nresult\r\n";
    redis::Bulk bulk(data, sizeof(data) - 1);
    std::cout << "payload_size\t:" << bulk.payload_size() << std::endl;
    std::cout << "total_size\t:" << bulk.total_size() << std::endl;
    std::cout << "absent_size\t:" << bulk.absent_size() << std::endl;
    assert(bulk.present_size() == sizeof(data) - 1);
  }

  {
    char data[] = "$6\r\nresult\r";
    redis::Bulk bulk(data, sizeof(data) - 1);

    std::cout << "payload_size\t:" << bulk.payload_size() << std::endl;
    std::cout << "total_size\t:" << bulk.total_size() << std::endl;
    std::cout << "absent_size\t:" << bulk.absent_size() << std::endl;
  }
}

int main() {
  loguru::g_stderr_verbosity = 8;
  std::cout << "============ BulkTest ============" << std::endl;
  BulkTest();

  std::cout << "============ BulkArrayTest ============" << std::endl;
  BulkArrayTest();
  return 0;
}

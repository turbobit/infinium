// Copyright (c) 2011-2014 The Cryptonote developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include "include_base_utils.h"
using namespace epee;

#include "cryptonote_basic_impl.h"
#include "string_tools.h"
#include "serialization/binary_utils.h"
#include "serialization/vector.h"
#include "cryptonote_format_utils.h"
#include "cryptonote_config.h"
#include "misc_language.h"
#include "common/base58.h"
#include "crypto/hash.h"
#include "common/int-util.h"

namespace cryptonote {

  /************************************************************************/
  /* Cryptonote helper functions                                          */
  /************************************************************************/
  //-----------------------------------------------------------------------------------------------
  size_t get_max_block_size()
  {
    return CRYPTONOTE_MAX_BLOCK_SIZE;
  }
  //-----------------------------------------------------------------------------------------------
  size_t get_max_tx_size()
  {
    return CRYPTONOTE_MAX_TX_SIZE;
  }
  //-----------------------------------------------------------------------------------------------
  namespace
  {
    const size_t log_fix_precision = 20;
    static_assert(1 <= log_fix_precision && log_fix_precision < sizeof(uint64_t) * 8 / 2 - 1, "Invalid log precision");

    uint64_t log2_fix(uint64_t x)
    {
      assert(x != 0);

      uint64_t b = UINT64_C(1) << (log_fix_precision - 1);
      uint64_t y = 0;

      while (x >= (UINT64_C(2) << log_fix_precision))
      {
        x >>= 1;
        y += UINT64_C(1) << log_fix_precision;
      }

      // 64 bits are enough, because of x < 2 * (1 << log_fix_precision) <= 2^32
      uint64_t z = x;
      for (size_t i = 0; i < log_fix_precision; i++)
      {
        z = (z * z) >> log_fix_precision;
        if (z >= (UINT64_C(2) << log_fix_precision))
        {
          z >>= 1;
          y += b;
        }
        b >>= 1;
      }

      return y;
    }
  }
  //-----------------------------------------------------------------------------------------------
  bool get_block_reward(size_t median_size, size_t current_block_size, uint64_t already_generated_coins, uint64_t &reward, const cryptonote::difficulty_type diff) {
    assert(diff != 0);
    assert(static_cast<uint64_t>(diff) < (UINT64_C(1) << (sizeof(uint64_t) * 8 - log_fix_precision)));
    uint64_t base_reward = log2_fix(diff << log_fix_precision) << 20;

    //make it soft
    if (median_size < CRYPTONOTE_BLOCK_GRANTED_FULL_REWARD_ZONE) {
      median_size = CRYPTONOTE_BLOCK_GRANTED_FULL_REWARD_ZONE;
    }

    if (current_block_size <= median_size) {
      reward = base_reward;
      return true;
    }

    if(current_block_size > 2 * median_size) {
      LOG_PRINT_L4("Block cumulative size is too big: " << current_block_size << ", expected less than " << 2 * median_size);
      return false;
    }

    assert(median_size < std::numeric_limits<uint32_t>::max());
    assert(current_block_size < std::numeric_limits<uint32_t>::max());

    uint64_t product_hi;
    uint64_t product_lo = mul128(base_reward, current_block_size * (2 * median_size - current_block_size), &product_hi);

    uint64_t reward_hi;
    uint64_t reward_lo;
    div128_32(product_hi, product_lo, static_cast<uint32_t>(median_size), &reward_hi, &reward_lo);
    div128_32(reward_hi, reward_lo, static_cast<uint32_t>(median_size), &reward_hi, &reward_lo);
    assert(0 == reward_hi);
    assert(reward_lo < base_reward);

    reward = reward_lo;
    return true;
  }
  //------------------------------------------------------------------------------------
  uint8_t get_account_address_checksum(const public_address_outer_blob& bl)
  {
    const unsigned char* pbuf = reinterpret_cast<const unsigned char*>(&bl);
    uint8_t summ = 0;
    for(size_t i = 0; i!= sizeof(public_address_outer_blob)-1; i++)
      summ += pbuf[i];

    return summ;
  }
  //-----------------------------------------------------------------------
  std::string get_account_address_as_str(const account_public_address& adr)
  {
    return tools::base58::encode_addr(CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX, t_serializable_object_to_blob(adr));
  }
  //-----------------------------------------------------------------------
  bool is_coinbase(const transaction& tx)
  {
    if(tx.vin.size() != 1)
      return false;

    if(tx.vin[0].type() != typeid(txin_gen))
      return false;

    return true;
  }
  //-----------------------------------------------------------------------
  bool get_account_address_from_str(uint64_t& prefix, account_public_address& adr, const std::string& str)
  {
    if (2 * sizeof(public_address_outer_blob) != str.size())
    {
      blobdata data;
      if (!tools::base58::decode_addr(str, prefix, data))
      {
        LOG_PRINT_L1("Invalid address format");
        return false;
      }

      if (!::serialization::parse_binary(data, adr))
      {
        LOG_PRINT_L1("Account public address keys can't be parsed");
        return false;
      }

      if (!crypto::check_key(adr.m_spend_public_key) || !crypto::check_key(adr.m_view_public_key))
      {
        LOG_PRINT_L1("Failed to validate address keys");
        return false;
      }
    }
    else
    {
      // Old address format
      prefix = CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX;

      std::string buff;
      if(!string_tools::parse_hexstr_to_binbuff(str, buff))
        return false;

      if(buff.size()!=sizeof(public_address_outer_blob))
      {
        LOG_PRINT_L1("Wrong public address size: " << buff.size() << ", expected size: " << sizeof(public_address_outer_blob));
        return false;
      }

      public_address_outer_blob blob = *reinterpret_cast<const public_address_outer_blob*>(buff.data());


      if(blob.m_ver > CRYPTONOTE_PUBLIC_ADDRESS_TEXTBLOB_VER)
      {
        LOG_PRINT_L1("Unknown version of public address: " << blob.m_ver << ", expected " << CRYPTONOTE_PUBLIC_ADDRESS_TEXTBLOB_VER);
        return false;
      }

      if(blob.check_sum != get_account_address_checksum(blob))
      {
        LOG_PRINT_L1("Wrong public address checksum");
        return false;
      }

      //we success
      adr = blob.m_address;
    }

    return true;
  }
  //-----------------------------------------------------------------------
  bool get_account_address_from_str(account_public_address& adr, const std::string& str)
  {
    uint64_t prefix;
    if(!get_account_address_from_str(prefix, adr, str))
      return false;

    if(CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX != prefix)
    {
      LOG_PRINT_L1("Wrong address prefix: " << prefix << ", expected " << CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX);
      return false;
    }

    return true;
  }
  //-----------------------------------------------------------------------

  bool operator ==(const cryptonote::transaction& a, const cryptonote::transaction& b) {
    return cryptonote::get_transaction_hash(a) == cryptonote::get_transaction_hash(b);
  }

  bool operator ==(const cryptonote::block& a, const cryptonote::block& b) {
    return cryptonote::get_block_hash(a) == cryptonote::get_block_hash(b);
  }
}

//--------------------------------------------------------------------------------
bool parse_hash256(const std::string str_hash, crypto::hash& hash)
{
  std::string buf;
  bool res = epee::string_tools::parse_hexstr_to_binbuff(str_hash, buf);
  if (!res || buf.size() != sizeof(crypto::hash))
  {
    std::cout << "invalid hash format: <" << str_hash << '>' << std::endl;
    return false;
  }
  else
  {
    buf.copy(reinterpret_cast<char *>(&hash), sizeof(crypto::hash));
    return true;
  }
}

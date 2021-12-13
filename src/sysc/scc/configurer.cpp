/*******************************************************************************
 * Copyright 2017, 2018 MINRES Technologies GmbH
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *******************************************************************************/
/*
 * configurer.cpp
 *
 *  Created on: 27.09.2017
 *      Author: eyck
 */

#include "configurer.h"
#include "report.h"
#include <json/json.h>
#ifdef HAS_CCI
#include <cci_configuration>
#include <cci_utils/broker.h>
#endif
#include <fstream>

namespace scc {
namespace {
inline auto get_sc_objects(sc_core::sc_object* obj = nullptr) -> const std::vector<sc_core::sc_object*>& {
    if(obj)
        return obj->get_child_objects();
    else
        return sc_core::sc_get_top_level_objects();
}

template <typename T, typename T1 = T>
auto check_n_assign(Json::Value& node, sc_core::sc_attr_base* attr_base) -> bool {
    auto* a = dynamic_cast<sc_core::sc_attribute<T>*>(attr_base);
    if(a != nullptr) {
        node[a->name()] = static_cast<T1>(a->value);
        return true;
    }
    return false;
}

void dump_config(configurer::broker_t const& broker, sc_core::sc_object* obj, Json::Value& parent) {
    auto mod = dynamic_cast<sc_core::sc_module*>(obj);
    Json::Value node{Json::objectValue};
    for(sc_core::sc_attr_base* attr_base : obj->attr_cltn()) {
        check_n_assign<int>(node, attr_base) || check_n_assign<unsigned>(node, attr_base) ||
                check_n_assign<long, Json::Int64>(node, attr_base) ||
                check_n_assign<unsigned long, Json::UInt64>(node, attr_base) ||
                check_n_assign<long long, Json::Int64>(node, attr_base) ||
                check_n_assign<unsigned long long, Json::UInt64>(node, attr_base) ||
                check_n_assign<bool>(node, attr_base) ||
                check_n_assign<float>(node, attr_base) || check_n_assign<double>(node, attr_base) ||
                check_n_assign<std::string>(node, attr_base) || check_n_assign<char*>(node, attr_base);
    }
#ifdef HAS_CCI
    const std::string hier_name{obj->name()};
    cci::cci_param_predicate pred{[hier_name](cci::cci_param_untyped_handle h) -> bool {
        std::string h_name{h.name()};
        auto sep = hier_name.length();
        if(h_name.length() > hier_name.length()) {
            auto path_match = h_name.compare(0, sep, hier_name) == 0;            // begins with hier_name
            auto sep_match = h_name[sep] == '.';                                 // has additional part
            auto tail_nomatch = h_name.substr(sep + 1).find('.') == h_name.npos; // but no other hierarchy separator
            return path_match && sep_match && tail_nomatch;
        } else
            return false;
    }};
    auto log_lvl_name = hier_name + ".log_level";
    auto handles = broker.get_param_handles(pred);
    auto log_lvl_set = false;
    for(auto& h : handles) {
        auto value = h.get_cci_value();
        auto paramname = std::string{h.name()};
        if(paramname == log_lvl_name)
            log_lvl_set = true;
        auto basename = paramname.substr(paramname.find_last_of('.') + 1);
        if(value.is_bool())
            node[basename] = value.get_bool();
        else if(value.is_int())
            node[basename] = value.get_int();
        else if(value.is_int64())
            node[basename] = static_cast<int64_t>(value.get_int64());
        else if(value.is_uint())
            node[basename] = value.get_uint();
        else if(value.is_uint64())
            node[basename] = static_cast<uint64_t>(value.get_uint64());
        else if(value.is_double())
            node[basename] = value.get_double();
        else if(value.is_string())
            node[basename] = value.get_string().c_str();
    }
    if(!log_lvl_set && mod) {
        auto val = broker.get_preset_cci_value(log_lvl_name);
        auto global_verb = static_cast<int>(get_logging_level());
        node["log_level"] = val.is_int() ? val.get_int() : global_verb;
    }
#endif
    for(auto* o : get_sc_objects(obj))
        dump_config(broker, o, node);
    if(!node.empty())
        parent[obj->basename()] = node;
}

#define CHECK_N_ASSIGN(TYPE, ATTR, VAL)                                                                                \
		{                                                                                                                  \
	auto* a = dynamic_cast<sc_core::sc_attribute<TYPE>*>(ATTR);                                                    \
	if(a != nullptr) {                                                                                             \
		a->value = VAL;                                                                                            \
		return;                                                                                                    \
	}                                                                                                              \
		}

void try_set_value(sc_core::sc_attr_base* attr_base, Json::Value& hier_val) {
    CHECK_N_ASSIGN(int, attr_base, hier_val.asInt());
    CHECK_N_ASSIGN(unsigned, attr_base, hier_val.asUInt());
    CHECK_N_ASSIGN(int64_t, attr_base, hier_val.asInt64());
    CHECK_N_ASSIGN(uint64_t, attr_base, hier_val.asUInt64());
    CHECK_N_ASSIGN(bool, attr_base, hier_val.asBool());
    CHECK_N_ASSIGN(float, attr_base, hier_val.asFloat());
    CHECK_N_ASSIGN(double, attr_base, hier_val.asDouble());
    CHECK_N_ASSIGN(std::string, attr_base, hier_val.asString());
    CHECK_N_ASSIGN(char*, attr_base, strdup(hier_val.asCString()));
}

void configure_cci_hierarchical(configurer::broker_t& broker, Json::Value const& node, std::string prefix) {
    if(node.size() > 0) {
        for(Json::Value::const_iterator itr = node.begin(); itr != node.end(); ++itr) {
            if(!itr.key().isString())
                return;
            auto key_name = itr.key().asString();
            auto hier_name=prefix.size() ? prefix + "." + key_name : key_name;
            auto val = node[key_name];
            if(val.isNull() || val.isArray())
                return;
            else if(val.isObject())
                configure_cci_hierarchical(broker, *itr, hier_name);
#ifdef HAS_CCI
            else {
                auto param_handle = broker.get_param_handle(hier_name);
                if(param_handle.is_valid()) {
                    if(val.isString()) {
                        param_handle.set_cci_value(cci::cci_value(val.asString()));
                    } else if(val.isBool()) {
                        param_handle.set_cci_value(cci::cci_value(val.asBool()));
                    } else if(val.isInt()) {
                        param_handle.set_cci_value(cci::cci_value(val.asInt()));
                    } else if(val.isInt64()) {
                        param_handle.set_cci_value(cci::cci_value(val.asInt64()));
                    } else if(val.isUInt()) {
                        param_handle.set_cci_value(cci::cci_value(val.asUInt()));
                    } else if(val.isUInt64()) {
                        param_handle.set_cci_value(cci::cci_value(val.asUInt64()));
                    } else if(val.isDouble()) {
                        param_handle.set_cci_value(cci::cci_value(val.asDouble()));
                    }
                } else {
                    if(val.isString()) {
                        broker.set_preset_cci_value(hier_name, cci::cci_value(val.asString()));
                    } else if(val.isBool()) {
                        broker.set_preset_cci_value(hier_name, cci::cci_value(val.asBool()));
                    } else if(val.isInt()) {
                        broker.set_preset_cci_value(hier_name, cci::cci_value(val.asInt()));
                    } else if(val.isInt64()) {
                        broker.set_preset_cci_value(hier_name, cci::cci_value(val.asInt64()));
                    } else if(val.isUInt()) {
                        broker.set_preset_cci_value(hier_name, cci::cci_value(val.asUInt()));
                    } else if(val.isUInt64()) {
                        broker.set_preset_cci_value(hier_name, cci::cci_value(val.asUInt64()));
                    } else if(val.isDouble()) {
                        broker.set_preset_cci_value(hier_name, cci::cci_value(val.asDouble()));
                    }
                }
            }
#endif
        }
    }
}

void configure_sc_attribute_hierarchical(sc_core::sc_object* obj, Json::Value& hier_val) {
    for(auto* attr : obj->attr_cltn()) {
        auto& val = hier_val[attr->name()];
        if(!val.isNull())
            try_set_value(attr, val);
    }
    for(auto* o : obj->get_child_objects()) {
        auto& val = hier_val[o->basename()];
        if(!val.isNull())
            configure_sc_attribute_hierarchical(o, val);
    }
}

void check_config_hierarchical(configurer::broker_t const& broker, Json::Value const& node, std::string const& prefix) {
    if(node.size() > 0) {
        for(Json::Value::const_iterator itr = node.begin(); itr != node.end(); ++itr) {
            if(!itr.key().isString())
                return;
            auto key_name = itr.key().asString();
            if(key_name == "log_level")
                continue; // virtual attribute
            auto hier_name=prefix.size() ? prefix + "." + key_name : key_name;
            auto val = node[key_name];
            if(val.isNull() || val.isArray())
                continue;
            else if(val.isObject()) {
                if(!sc_core::sc_find_object(hier_name.c_str())) {
                    throw std::domain_error(hier_name);
                }
                check_config_hierarchical(broker, *itr, hier_name);
            }
#ifdef HAS_CCI
            else {
                auto* obj = sc_core::sc_find_object(prefix.c_str());
                auto* attr = obj->get_attribute(key_name);
                if(!attr) {
                    auto param_handle = broker.get_param_handle(hier_name);
                    if(!param_handle.is_valid()) {
                        throw std::invalid_argument(hier_name);
                    }
                }
            }
#endif
        }
    }
}
}

struct configurer::ConfigHolder {
    Json::Value v;
};

configurer::configurer(const std::string& filename)
: base_type(sc_core::sc_module_name("configurer"))
#ifdef HAS_CCI
, cci_originator("configurer")
, cci_broker(cci::cci_get_global_broker(cci_originator))
#endif
{
    if(filename.length() > 0) {
        std::ifstream is(filename);
        if(is.is_open()) {
            root.reset(new ConfigHolder);
            try {
                is >> root->v;
                configure_cci_hierarchical(cci_broker, root->v, "");
                config_valid = true;
            } catch(Json::RuntimeError& e) {
                SCCERR() << "Could not parse input file " << filename << ", reason: " << e.what();
            }
        } else {
            SCCERR() << "Could not open input file " << filename;
        }
    }
    // register_simulation_phase_callback(sc_core::sc_status::SC_END_OF_ELABORATION);
}

configurer::~configurer(){}

void configurer::dump_hierarchy(std::ostream& os, sc_core::sc_object* obj) {
    if(obj) {
        os << obj->name() << " of type " << typeid(*obj).name() << "\n";
        for(auto* o : obj->get_child_objects())
            dump_hierarchy(os, o);
    } else {
        for(auto* o : sc_core::sc_get_top_level_objects())
            dump_hierarchy(os, o);
    }
}

void configurer::dump_configuration(std::ostream& os, sc_core::sc_object* obj) {
    Json::Value root{Json::objectValue};
    for(auto* o : get_sc_objects(obj)) {
        dump_config(cci_broker, o, root);
    }
    // For convenience, use `writeString()` with a specialized builder.
    Json::StreamWriterBuilder wbuilder;
    wbuilder["indentation"] = "\t";
    wbuilder["enableYAMLCompatibility"] = true;
    wbuilder["commentStyle"] = "None";
    wbuilder.newStreamWriter()->write(root, &os);
    // another (default) option:
    // os << root;
}

void configurer::configure() {
    if(config_valid && root)
        for(auto* o : sc_core::sc_get_top_level_objects()) {
            Json::Value& val = root->v[o->name()];
            if(!val.isNull())
                configure_sc_attribute_hierarchical(o, val);
        }
}

auto get_value_from_hierarchy(const std::string& hier_name, Json::Value& value) -> Json::Value& {
    size_t npos = hier_name.find_first_of('.');
    Json::Value& val = value[hier_name.substr(0, npos)];
    if(val.isNull() || npos == std::string::npos || !val.isObject())
        return val;
    return get_value_from_hierarchy(hier_name.substr(npos + 1, hier_name.size()), val);
}

void configurer::set_configuration_value(sc_core::sc_attr_base* attr_base, sc_core::sc_object* owner) {
    if(root){
        std::string name(owner->name());
        name += ".";
        name += attr_base->name();
        size_t npos = name.find_first_of('.');
        Json::Value& val = get_value_from_hierarchy(name, root->v[name.substr(0, npos)]);
        if(!val.isNull())
            try_set_value(attr_base, val);
    }
}


void configurer::end_of_elaboration() {
    try {
        if(root) check_config_hierarchical(cci_broker, root->v, "");
    } catch(std::domain_error& e) {
        SCCFATAL(this->name()) << "Illegal hierarchy name: '" << e.what() << "'";
    } catch(std::invalid_argument& e){
        SCCFATAL(this->name()) << "Illegal parameter name: '" << e.what() << "'";
    }

}
#ifdef WITH_SIM_PHASE_CALLBACKS
void configurer::simulation_phase_callback()  {
    if(root) check_config_hierarchical(root->v, "");
}
#endif

void init_cci(std::string name) {
#ifdef HAS_CCI
    thread_local static cci_utils::broker broker(name);
    cci::cci_register_broker(&broker);
#endif
}
}

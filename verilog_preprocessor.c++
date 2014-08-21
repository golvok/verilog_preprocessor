/* TODOS:
 * - find constants in not base 12 that have implicit length - add 32 to front
 * - convert multidim wire thing to use pure indicies
 * - generate loops
 * - add #(...) to defparam conversion
 * - remove signed, arithmetic shifts?
 * - use WireInfo in module reclaration
 *    also, make sure WireInfo handles spaces (or lack thereof) ...
 */

#include <iostream>
#include <sstream>
#include <memory>
#include <unordered_map>
#include <vector>
#include <cstring>
#include <cctype>
#include <deque>

using namespace std;

class Macro {
public:
	Macro(istream& is);
	string getName() { return name; }
	string expand(const vector<string>& args);
	bool isEmptyMacro() { return body.size() == 0; }
private:
	bool is_function_like;
	vector<string> params;
	string body;
	string name;
};

class WireInfo {
public:
	const string& getName() { return name; }
	const string& getType() { return type; }
	size_t getDimensionSize(size_t dim_number) {
		auto dim_info = dimension_sizes.at(dim_number-1);
		return dim_info.second - dim_info.first;
	};
	size_t getLowerBound(size_t dim_number) { return dimension_sizes.at(dim_number-1).first;  }
	size_t getUpperBound(size_t dim_number) { return dimension_sizes.at(dim_number-1).second; }
	size_t getNumDimensions() { return dimension_sizes.size(); }
	WireInfo() : name(), type(), dimension_sizes() {}
	string makeDeclaration();
	static std::pair<bool,WireInfo> parseWire(string&);
private:
	string name;
	string type;
	vector<std::pair<size_t,size_t>> dimension_sizes;
};

void macro_expansion_pass(istream& is, ostream& os);
void module_redeclaration_pass(istream& is, ostream& os);
void twodim_reduction_pass(istream& is, ostream& os);

vector<string> parseParamList(istream& is);
string readUntil(istream& from, const char* until, bool ignore_initial_whitespace);
vector<string> splitAndTrim(const string& s, char delim);
string& trim(string& str);
string trim(const string& str);
string skipToNextLineIfComment(char prev_char, char c, istream& is);

int mathEval(istream& expr);
int mathEval(const string& s) {
	istringstream iss(s);
	return mathEval(iss);
}

int main() {

	// stringstream with_reduced_twodims;
	{
		stringstream with_redeclared_modules;
		{
			stringstream with_expanded_macros;
			{
				macro_expansion_pass(cin, with_expanded_macros);
			}
			module_redeclaration_pass(with_expanded_macros, with_redeclared_modules);
		}
		twodim_reduction_pass(with_redeclared_modules, cout);
	}

	return 0;
}

void macro_expansion_pass(istream& is, ostream& os) {
	unordered_map<string,Macro> name2macro;

	char prev_char = '\0';
	while (true) {
		int c = is.get();
		if (is.eof()) {
			break;
		}

		string comment_line = skipToNextLineIfComment(prev_char,c,is);
		if (comment_line.size() > 0) {
			os.put(c);
			c = is.get();
			os << comment_line;
		}
		if (is.eof()) {
			break;
		}

		if (c == '`') {
			string directive = trim(readUntil(is, ":;-+/*%){}[] (\n", true)); // arg.. regexes
			if (directive == "define") {
				Macro m(is);
				name2macro.insert(make_pair(m.getName(),m));
				if (m.isEmptyMacro()) {
					os << "`define " << m.getName() << '\n';
				}
			} else if (directive == "ifdef" || directive == "else" || directive == "endif") {
				os << '`' << directive;
			} else {
				auto lookup_result = name2macro.find(directive);
				if (lookup_result == name2macro.end()) {
					// string lines = readUntil(is,"\n",false);
					// lines += is.get();
					// lines += readUntil(is,"\n",false);
					// lines += is.get();
					// lines += readUntil(is,"\n",false);
					// lines += is.get();
					cerr 
						<< "macro \""<<directive<<"\" has not been defined\n"
						// << "near " << lines << "\n"
						;
					exit(1);
				} else {
					vector<string> param_list;
					if (is.peek() == '(') {
						param_list = parseParamList(is);
					} // leave empty in other cases

					os << lookup_result->second.expand(param_list);
				}
			}
		} else {
			os.put(c);
		}
		prev_char = c;
	}
}

void module_redeclaration_pass(istream& is, ostream& os) {
	int prev_char = ' ';
	while (true) {
		int c = is.get();
		if (is.eof()) {
			break;
		}

		string comment_line = skipToNextLineIfComment(prev_char,c,is);
		if (comment_line.size() > 0) {
			os.put(c);
			c = is.get();
			os << comment_line;
		}
		if (is.eof()) {
			break;
		}

		if (c == 'm' && isspace(prev_char)) {
			is.putback(c);
			string module_token;
			is >> module_token;
			os << module_token;
			if (module_token == "module") {
				string module_name = readUntil(is, "(", false);
				os << module_name;
				vector<string> module_params = parseParamList(is);
				vector<string> module_param_names;
				vector<string> module_param_types;

				os << '(';

				bool needs_redecl = false;
				for (auto& param : module_params) {
					if (param.find("input") || param.find("ouput")) {
						needs_redecl = false;
						break;
					}
				}

				for (auto param = module_params.begin(); param != module_params.end(); ++param) {
					string::size_type last_space_index = param->find_last_of(" ");
					// (NOTE: whitespace is trimmed in parseParamList)
					if (needs_redecl) {
						string name = param->substr(last_space_index);
						os << name;
						module_param_names.push_back(name);
						module_param_types.push_back(param->substr(0, last_space_index));
					} else {
						os << *param;
					}
					if ((param + 1) != module_params.end()) {
						os << ",\n";
					}

				}

				os << ')';

				{
					string rest_of_decl = readUntil(is,";",false);
					os << rest_of_decl << (char)is.get() << '\n';
				}

				if (needs_redecl) {
					for (size_t i = 0; i < module_params.size(); ++i) {
						string::size_type position_of_reg = string::npos;
						if (module_param_types[i].find("output") != string::npos 
							&& (position_of_reg = module_param_types[i].find("reg")) != string::npos) {
							// the case of an output reg
							string rest_of_type = module_param_types[i].substr(position_of_reg + 3);
							os << "output" << rest_of_type << module_param_names[i] << ";\n";
							os << "reg   " << rest_of_type << module_param_names[i] << ";\n";

						} else {
							os << module_params[i] << ";\n";
						}
					}
				}
			}
		} else {
			os.put(c);
		}
		prev_char = c;
	}
}

void twodim_reduction_pass_redecl(
	istream& is, ostream& os, unordered_map<string,WireInfo>& name2size);
void twodim_reduction_pass_rewrite(
	istream& is, ostream& os, unordered_map<string,WireInfo>& name2size);

void twodim_reduction_pass(istream& is, ostream& os) {
	unordered_map<string,WireInfo> name2size;
	stringstream with_redecl;
	twodim_reduction_pass_redecl(is, with_redecl, name2size);
	twodim_reduction_pass_rewrite(with_redecl, os, name2size);
}

void twodim_reduction_pass_redecl(
	istream& is, ostream& os, unordered_map<string,WireInfo>& name2size) {
	int prev_char = ' ';
	while (true) {
		int c = is.get();
		if (is.eof()) {
			break;
		}

		string comment_line = skipToNextLineIfComment(prev_char,c,is);
		if (comment_line.size() > 0) {
			os.put(c);
			c = is.get();
			os << comment_line;
		}
		if (is.eof()) {
			break;
		}

		if ((c == 'r' || c == 'w') && isspace(prev_char)) {
			is.putback(c);
			string decl;
			is >> decl;
			if (decl == "reg" || decl == "wire") {
				decl += readUntil(is, ";", false);
				trim(decl);
				bool success = false;
				WireInfo wire_info;
				std::tie(success,wire_info) = WireInfo::parseWire(decl);
				if (success && wire_info.getNumDimensions() > 1) {
					is.get(); // consume ';'
					name2size.insert(std::make_pair(wire_info.getName(),wire_info));
					os << wire_info.makeDeclaration();
				} else {
					os << decl;
				}
			} else {
				os << decl;
			}
		} else {
			os.put(c);
		}
		prev_char = c;
	}

	// cerr << "found twodims:\n";
	// for (auto twodim = name2size.begin(); twodim != name2size.end(); ++twodim) {
	// 	cerr << "name="<<twodim->first<<" range="<<twodim->second.first<<"-"<<twodim->second.second<<"\n";
	// }
}

void twodim_reduction_pass_rewrite(
	istream& is,
	ostream& os,
	unordered_map<string,WireInfo>& name2size
) {

	unordered_multimap<size_t,string> length2name;
	size_t longest_name = 2;

	for (const auto& name_and_size : name2size) {
		length2name.insert(make_pair(name_and_size.first.size(),name_and_size.first));
		if (name_and_size.first.size() > longest_name) {
			longest_name = name_and_size.first.size();
		}
	}

	deque<char> last_few_chars;
	bool flush_buffer = false;

	while (true) {
		while (last_few_chars.size() < longest_name) {
			last_few_chars.push_back(is.get());
			if (is.eof()) {
				last_few_chars.pop_back();
				break;
			}
		}

		string comment_line = skipToNextLineIfComment(last_few_chars[0],last_few_chars[1],is);
		if (comment_line.size() > 0) {
			for (char c : comment_line) {
				last_few_chars.push_back(c);
			}
			flush_buffer = true;
			goto continue_and_ouput;
		}

		if (is.eof()) {
			// flush buffer & exit
			while (!last_few_chars.empty()) {
				os.put(last_few_chars.front());
				last_few_chars.pop_front();
			}
			break;
		}

		
		// see if the last n chars match a declared twodim (of length n)
		{
			string found_match = "";
			// cerr << "comparing with `" << last_few_chars << "'\n";
			for (size_t i = 1; i <= longest_name; ++i) { // must iterate from smallest to largest
				auto range = length2name.equal_range(i);
				if (range.first != length2name.end()) {
					// have entries of this size
					// cerr << "looking at twodims of size "<< i << '\n';
					for (auto l2name_iter = range.first; l2name_iter != range.second; ++l2name_iter) {
						string& name = l2name_iter->second;
						// cerr << "\tlooking at `" << name << "'\n";
						bool found_divergence = false;
						auto last_char = last_few_chars.rbegin();
						for (
							auto char_in_name = name.rbegin();
							char_in_name != name.rend() && last_char != last_few_chars.rend();
							++char_in_name, ++last_char
						) {
							if (*char_in_name != *last_char) {
								found_divergence = true;
								break;
							}
						}
						if (!found_divergence) {
							found_match = name;
							// cerr << name << " matches\n";
						}
					}
				}
			}

			// sub in the match

			if (found_match.size() > 0) {
				string next_chars;
				while (isspace(is.peek())) {
					next_chars += is.get();
				}
				if (is.peek() == '[') {
					is.get(); // consume '['
					stringstream new_suffix;
					string inside_brackets = readUntil(is,"]",false);
					istringstream inside_brackets_ss(inside_brackets);
					int evaluated_insides;
					bool good = false;
					try {
						evaluated_insides = mathEval(inside_brackets_ss);
						good = true;
					} catch (const std::invalid_argument&) {
					} catch (const std::out_of_range&) {
					}

					if (!good) {
						last_few_chars.push_back('[');
						for (char c : inside_brackets) {
							last_few_chars.push_back(c);
						}
						flush_buffer = true;
						goto continue_and_ouput;
					}

					new_suffix << "_" << evaluated_insides << next_chars;
					is.get(); // consume ']';
					while (true) {
						char c = new_suffix.get();
						if (new_suffix.eof()) {break;}
						last_few_chars.push_back(c);
					}
					flush_buffer = true;
				} else {
					// didn't find a use. Shove it all back in
					for (auto next_char = next_chars.rbegin(); next_char != next_chars.rend(); ++next_char) {
						is.putback(*next_char);
					}
				}
			}
		}

		continue_and_ouput:
		while (
			last_few_chars.size() >= longest_name
			|| (flush_buffer && !last_few_chars.empty())
		) {
			os.put(last_few_chars.front());
			// cerr.put(last_few_chars.front());
			last_few_chars.pop_front();
		}
		flush_buffer = false;
	}

}

Macro::Macro(istream& is)
	: is_function_like(false)
	, params()
	, body()
	, name() {

	name = trim(readUntil(is, "\n (", true));

	char next_char = is.get();
	while(next_char == ' ') {next_char = is.get();}
	is.putback(next_char);
	if (next_char == '(') {
		// if the next char is a '(' then it is a function-like
		is_function_like = true;
	} else {
		// case of siple macro
		is_function_like = false;
	}

	if (is_function_like) {
		params = parseParamList(is);
	}

	bool found_backslash = false;
	string line;
	while (true) {
		line += readUntil(is,"\\\n", false);
		if (is.get() == '\\') {
			found_backslash = true;
		} else {
			line += '\n';
			body += line;
			line.clear(); 
			if (!found_backslash) {
				break;
			}
			found_backslash = false;
		}
	}

	trim(body);

	// cerr
	// <<	"found definition of macro `"<<name<<"'\n"
	// <<	"params = "
	// ;
	// for (auto& param : params) {
	// 	cerr << param << " ,";
	// }
	// cerr <<	"$\nbody = "<<body<<"\n";
}

string Macro::expand(const vector<string>& args) {
	if (args.size() != params.size()) {
		cerr << 
			"num given args ("<<args.size()<<") and expected params ("<<params.size()<<")"
			" differ for macro \""<<name<<"\"\n";
		exit(1);
	}

	string expanded_body = body; // copy the unexpanded body;

	for (size_t i = 0; i < params.size(); ++i) {
		size_t pos = 0;
		while ((pos = expanded_body.find(params[i], pos)) != std::string::npos) {
			expanded_body.replace(pos, params[i].length(), args[i]);
			pos += args[i].length();
		}
	}

	return expanded_body;
}

string WireInfo::makeDeclaration() {
	ostringstream builder;
	for (size_t i = getLowerBound(2); i <= getUpperBound(2); ++i) {
		builder 
			<< getType() << " [" << getUpperBound(1) << ":" << getLowerBound(1) << "] "
			<< getName() << "_" << i << ";\n";
	}
	return builder.str();
}

std::pair<size_t,size_t> parseVectorDeclation(const string& decl) {
	pair<size_t,size_t> result;

	istringstream second_dim_decl(
		trim(decl)
	);
	
	result.first = mathEval(readUntil(second_dim_decl, ":", true));
	second_dim_decl.get(); // consume ':'
	result.second = mathEval(second_dim_decl);
	return result;
}

std::pair<bool,WireInfo> WireInfo::parseWire(string& decl) {
	cerr << "parsing wire/reg: `" << decl << "'\n";
	
	bool success = false;
	WireInfo wire_info;
	trim(decl);

	vector<string::size_type> bracket_locations {};
	while(true) {
		size_t prev_location = 0;
		if (bracket_locations.size() != 0) {
			prev_location = bracket_locations.back();
		}
		string::size_type next_bracket_location = decl.find_first_of("[", prev_location + 1);
		if (next_bracket_location == string::npos) {
			break;
		} else {
			bracket_locations.push_back(next_bracket_location);
		}
	}

	string::size_type end_of_type = decl.find_first_of(" [", 0);

	if (end_of_type == string::npos) {
		success = false;
		goto skip_to_return;
	}

	wire_info.type = trim(decl.substr(0, end_of_type));
	
	if (bracket_locations.size() == 0) {
		cerr << "is nodim\n";
		wire_info.dimension_sizes.push_back(make_pair(0,0));
		wire_info.name = trim(
			decl.substr(
				decl.find_last_of(" ]") + 1,
				string::npos
			)
		);
		success = false;
	} else {
		for (auto bracket_location : bracket_locations) {
			std::pair<size_t,size_t> dim_pair = parseVectorDeclation(
				decl.substr(
					bracket_location + 1,
					decl.find_first_of("]",bracket_location) - (bracket_location + 1)
				)
			);
			if (dim_pair.first > dim_pair.second) {
				std::swap(dim_pair.first, dim_pair.second);
			}
			wire_info.dimension_sizes.push_back(dim_pair);
			cerr << "dim\n";
		}
		string::size_type first_closing_bracket = decl.find_first_of("]", 0);
		string::size_type end_of_name = decl.find_first_of(" [", first_closing_bracket + 2);
		wire_info.name = trim(
			decl.substr(
				first_closing_bracket + 1,
				end_of_name - (first_closing_bracket + 1)
			)
		);
		success = true;
	}


	skip_to_return:

	cerr 
		<< 	"parsed WireInfo = {\n"
			"\tname = \"" << wire_info.getName() << "\",\n"
			"\ttype = \"" << wire_info.getType() << "\",\n"
			"\tnum_dims = " << wire_info.getNumDimensions() << ",\n"
	;
	for (size_t i = 1; i <= wire_info.getNumDimensions(); ++i) {
		cerr 
		<<	"\tdimension["<<i<<"] = ["
			<< wire_info.getLowerBound(i) << ':' << wire_info.getUpperBound(i)
		<< "],\n";
	}
	cerr << "}\n";

	return std::make_pair(success, wire_info); 
}

vector<string> parseParamList(istream& is) {
	char first_char;
	is >> first_char;
	if (first_char != '(') {
		cerr << "param list doesn't start with a '(' ( is '"<<first_char<<"')\n";
		exit(1);
	}

	string param_list = readUntil(is,")",true);
	is.get(); // consume ')'

	// cerr << "param_list=\"" << param_list << "\"\n";

	return splitAndTrim(param_list, ',');
}

string readUntil(istream& from, const char* until, bool ignore_initial_whitespace) {
	string result;
	bool first_time = true;
	bool newline_in_search_set = strchr(until,'\n');
	while (true) {
		char c;
		if (first_time && ignore_initial_whitespace) {
			from >> c;
		} else {
			c = from.get();
		}
		if (from.eof()) {
			break;
		}		
		if (strchr(until,c) != NULL || (newline_in_search_set && (c == '\r') ) ) {
			// found a matching char.
			if (c == '\n' && newline_in_search_set && from.peek() == '\r') {
				from.get(); // consume '\r'
			}
			from.putback(c);
			break;
		} else {
			result += c;
		}
		first_time = false;
	}
	return result;
}

vector<string> splitAndTrim(const string& s, char delim) {
	istringstream ss(s);
	vector<string> result;
	string token;
	while (getline(ss, token, delim)) {
		trim(token);
		result.push_back(token);
	}
	return result;
}

string& trim(string& str) {
	str.erase(str.find_last_not_of(" \n\r\t") + 1, string::npos);
	str.erase(0, str.find_first_not_of(" \n\r\t"));
	return str;
}

string trim(const string& str) {
	string result = str;
	return trim(result);
}

string skipToNextLineIfComment(char prev_char, char c, istream& is) {
	if (prev_char == '/' && c == '/') {
		return readUntil(is,"\n",false);
	}
	return "";
}


/// algorithm from http://en.wikipedia.org/wiki/Operator-precedence_parser
// expression ::= primary OPERATOR primary
// primary ::= '(' expression ')' | NUMBER | VARIABLE

unordered_map<char,int> precedence_map = {
	{ '+' , 1 },
	{ '-' , 1 },
	{ '*' , 2 },
	{ '/' , 2 },
	{ '%' , 2 }
};

int expression(istream& is, int lhs, int min_precedence);
int parse_primary(istream& is);
int do_binary_op(int lhs, int rhs, char op);

int mathEval(istream& expr) {
	return expression(expr, parse_primary(expr), 0);
}

/**
 * Parse out a primary expression, something that is self-contained, and
 * has a value; a number, a parentheses enclosed expression, or a
 * constant (though the last one in currently unsupported)
 */
int parse_primary(istream& is) {
	// read until you've found something that definite ends,
	// or starts, (in the case of ')' ) a primary expression
	string primary = trim(readUntil(is, "+-*/%( ",true));
	int result = 0;
	if (is.peek() == '(') {
		if (primary.size() != 0) {
			cerr << "Bad primary. Unexpected `" <<primary<< "' in `" <<primary<<readUntil(is,"\n",false)<< "'\n";
			exit(1);
		}
		is.get(); // consume '('
		string subexpr = readUntil(is, ")", true);
		if (is.eof()) {cerr << "no matching ')'\n"; exit(1);}
		is.get(); // consume ')'m
		stringstream subexprss(trim(subexpr));
		result = expression(subexprss, parse_primary(subexprss), 0);
	} else {
		try {
			// TODO: parse variable names here
			result = stoi(primary);
		} catch (const std::invalid_argument& e) {
			cerr << "bad int: " << primary << "\n"; throw e;
		} catch (const std::out_of_range& e) {
			cerr << "int out of range: " << primary << "\n"; throw e;
		}
	}
	return result;
}

// from http://en.wikipedia.org/wiki/Operator-precedence_parser
// parse_expression (lhs, min_precedence)
//     while the next token is a binary operator whose precedence is >= min_precedence
//         op := next token
//         rhs := parse_primary ()
//         while the next token is a binary operator whose precedence is greater
//                  than op's, or a right-associative operator
//                  whose precedence is equal to op's
//             lookahead := next token
//             rhs := parse_expression (rhs, lookahead's precedence)
//         lhs := the result of applying op with operands lhs and rhs
//     return lhs

int expression(istream& is, int lhs, int min_precedence) {
	while (true) {
		char this_op;
		if (!(is >> this_op)) {
			break;
		}
		int this_op_precedence = precedence_map.find(this_op)->second;
		if (this_op_precedence < min_precedence) {
			is.putback(this_op);
			return lhs;
		}
		int rhs = parse_primary(is);
		while (true) {
			char next_op = 0;
			if (!(is >> next_op)) {
				break;
			}
			// TODO: support right-associative operators (eg. unary ones)
			int next_op_precedence = precedence_map.find(next_op)->second;
			is.putback(next_op);
			if (next_op_precedence <= this_op_precedence) {
				break;
			}
			rhs = expression(is, rhs, next_op_precedence);
		}
		lhs = do_binary_op(lhs, rhs, this_op);
	}
	return lhs;
}

int do_binary_op(int lhs, int rhs, char op) {
	switch (op) {
		case '*': return lhs * rhs;
		case '/': return lhs / rhs;
		case '+': return lhs + rhs;
		case '-': return lhs - rhs;
		case '%': return lhs % rhs;
		default :
			cerr << "bad operator : '" << op << "'\n";
			exit(1);
			return 0;
	}
}

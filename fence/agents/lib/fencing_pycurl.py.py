__author__ = 'Ondrej Mular <omular@redhat.com>'
__all__ = ["FencingPyCurl"]

import pycurl
import sys
import atexit
import json
import StringIO
import time
import logging
import pprint

## do not add code here.
#BEGIN_VERSION_GENERATION
RELEASE_VERSION = ""
REDHAT_COPYRIGHT = ""
BUILD_DATE = ""
#END_VERSION_GENERATION

class FencingPyCurl():
	active = False
	input_file = None
	output_file = None
	options = None
	actions = None
	request_index = 0
	output_buffer = None
	write_function = None
	pycurl_obj = None

	def __init__(self):
		if not FencingPyCurl.active and (FencingPyCurl.input_file or FencingPyCurl.output_file):
			FencingPyCurl.active = True
			logging.debug("FencingPyCurl is active now")
		if FencingPyCurl.active:
			self.options = {
				"request": {},
				"response": {},
				"time": {}
			}

			self.output_buffer = StringIO.StringIO()
			if FencingPyCurl.actions is None:
				if FencingPyCurl.input_file:
					logging.debug("Reading input file.")

					try:
						with open(FencingPyCurl.input_file, "r") as f:
							FencingPyCurl.actions = json.load(f)
					except Exception as e:
						logging.debug("Reading input file (%s) failed: %s" % (FencingPyCurl.input_file, e.message))

				if FencingPyCurl.output_file:
					logging.debug("output file detected")
					if not FencingPyCurl.actions:
						FencingPyCurl.actions = []
						FencingPyCurl.request_index = 0

		self.pycurl_obj = pycurl.Curl()

	def setopt(self, opt, value):
		if FencingPyCurl.active:
			if opt == pycurl.WRITEFUNCTION:
				self.write_function = value
				value = self.output_buffer.write
			else:
				self.options["request"][str(opt)] = value
		return self.pycurl_obj.setopt(opt, value)

	def perform(self):
		if FencingPyCurl.active:
			if FencingPyCurl.input_file:
				perform_start = time.time()
				if self.options["request"] == FencingPyCurl.actions[FencingPyCurl.request_index]["request"]:
					self.options["response"] = FencingPyCurl.actions[FencingPyCurl.request_index]["response"]
					if self.write_function:
						self.write_function(self.options["response"]["output"])
					diff = FencingPyCurl.actions[FencingPyCurl.request_index]["time"]["perform_duration"] - (time.time() - perform_start)
					if diff > 0:
						logging.debug("sleeping for: %s" % str(diff))
						time.sleep(diff)
					FencingPyCurl.last_request_time = time.time()
				else:
					print "Request:"
					pprint.pprint(self.options["request"])
					print "Expected:"
					pprint.pprint(FencingPyCurl.actions[FencingPyCurl.request_index]["request"])
					raise Exception("Invalid request")
			else:
				start_time = time.time()
				self.pycurl_obj.perform()
				self.options["time"]["perform_duration"] = FencingPyCurl.last_request_time - start_time
				self.options["response"]["output"] = self.output_buffer.getvalue()
				if self.write_function:
					self.write_function(self.options["response"]["output"])
				if FencingPyCurl.output_file:
					FencingPyCurl.actions.append(self.options)
			FencingPyCurl.request_index += 1
		else:
			return self.pycurl_obj.perform()

	def unsetopt(self, opt):
		if FencingPyCurl.active:
			del self.options["request"][str(opt)]
		return self.pycurl_obj.unsetopt(opt)

	def getinfo(self, opt):
		value = self.pycurl_obj.getinfo(opt)
		if FencingPyCurl.active:
			if FencingPyCurl.input_file:
				value = self.options["response"][str(opt)]
			else:
				self.options["response"][opt] = value
		return value

	def reset(self):
		if FencingPyCurl.active:
			self.options.clear()
		return self.pycurl_obj.reset()

	def close(self):
		return self.pycurl_obj.close()

	@staticmethod
	def save_log_to_file():
		if FencingPyCurl.output_file and FencingPyCurl.actions:
			logging.debug("Writing log to file: %s" % FencingPyCurl.output_file)
			try:
				with open(FencingPyCurl.output_file, "w") as f:
					json.dump(FencingPyCurl.actions, f, sort_keys=True, indent=4, separators=(',', ': '))
			except Exception as e:
				logging.debug("Writing log to file (%s) failed: %s" % (FencingPyCurl.output_file, e.message))


def get_and_remove_arg(arg, has_value=True):
	logging.debug("Getting arg: %s (has_value: %s)" % (arg, str(has_value)))
	if not has_value:
		if arg in sys.argv:
			sys.argv.remove(arg)
			logging.debug("%s: True" % arg)
			return True
	if arg in sys.argv:
		index = sys.argv.index(arg)
		sys.argv.remove(arg)
		if len(sys.argv) > index:
			value = sys.argv[index]
			sys.argv.remove(value)
			logging.debug("%s: %s" % (arg, value))
			return value
	return None

FencingPyCurl.input_file = get_and_remove_arg("--fencing_pycurl-log-in")
FencingPyCurl.output_file = get_and_remove_arg("--fencing_pycurl-log-out")

if FencingPyCurl.output_file:
	atexit.register(FencingPyCurl.save_log_to_file)

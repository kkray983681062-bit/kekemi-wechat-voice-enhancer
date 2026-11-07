import queue
import unittest
from wevoice.gui import App


class UiStateTests(unittest.TestCase):
    def test_background_read_does_not_drop_apply_click(self):
        app = App.__new__(App)
        app.busy = app.closing = app.polling = False
        app.message = ''
        app.commands = queue.Queue()
        app.draw = lambda: None
        app.request('poll')
        app.request('apply')
        self.assertEqual(app.commands.qsize(), 2)
        self.assertEqual(app.commands.get(), ('poll', None))
        self.assertEqual(app.commands.get(), ('apply', None))
        self.assertTrue(app.busy)


if __name__ == '__main__':
    unittest.main()

from sanic import Sanic
from sanic.response import html

app = Sanic("sanic-app")


@app.route('/')
async def test(request):
    return html("Hello from sanic!", 200)

if __name__ == '__main__':
    app.run()
